#include "StarWorkshopPreview.hpp"
#include "StarWorkshopLogic.hpp"
#include "StarImageDecode.hpp"
#include "StarGuiContext.hpp"
#include "StarLogging.hpp"
#include "StarException.hpp"

#include <algorithm>

namespace Star {

static char const* const WorkshopPreviewPlaceholder = "/interface/modsmenu/modicon.png";

ImageConstPtr WorkshopPreviewCache::get(String const& itemId, String const& url) {
  if (!m_entries.contains(itemId)) {
    if (m_entries.size() >= MaxEntries)
      evictOldest();

    Entry entry;
    if (isAllowedWorkshopPreviewUrl(url)) {
      // HttpClient only implements requestAsync; getAsync and the other
      // helpers are declared in the header but never defined.
      HttpRequest request;
      request.method = "GET";
      request.url = url;
      entry.fetch = HttpClient::requestAsync(request);
    } else {
      Logger::debug("Not fetching Workshop preview for {} from disallowed url '{}'", itemId, url);
    }
    m_entries[itemId] = std::move(entry);
  }

  auto& entry = m_entries[itemId];
  entry.lastUsed = ++m_useCounter;
  return entry.image;
}

void WorkshopPreviewCache::update() {
  for (auto& pair : m_entries) {
    auto& entry = pair.second;
    if (!entry.fetch || !entry.fetch->done())
      continue;

    try {
      auto const& response = entry.fetch->get();
      if (!response.error.empty() || response.statusCode != 200) {
        Logger::debug("Workshop preview fetch for {} failed: {} {}", pair.first, response.statusCode, response.error);
      } else if (auto image = decodeImage(ByteArray(response.body.utf8Ptr(), response.body.utf8Size()))) {
        entry.image = make_shared<Image>(fitImage(*image, MaxImageSide));
      } else {
        Logger::debug("Could not decode Workshop preview for {}", pair.first);
      }
    } catch (std::exception const& e) {
      Logger::debug("Workshop preview fetch for {} threw: {}", pair.first, outputException(e, false));
    }

    entry.fetch = {};
  }
}

void WorkshopPreviewCache::evictOldest() {
  Maybe<String> oldestId;
  uint64_t oldestUse = 0;
  for (auto const& pair : m_entries) {
    if (!oldestId || pair.second.lastUsed < oldestUse) {
      oldestId = pair.first;
      oldestUse = pair.second.lastUsed;
    }
  }
  if (oldestId)
    m_entries.remove(*oldestId);
}

WorkshopPreviewWidget::WorkshopPreviewWidget() {}

void WorkshopPreviewWidget::setImage(ImageConstPtr image) {
  m_image = std::move(image);
}

void WorkshopPreviewWidget::renderImpl() {
  auto guiContext = context();
  Vec2F position = Vec2F(screenPosition());
  Vec2F box = Vec2F(size());

  if (!m_image) {
    Vec2F placeholderSize = Vec2F(guiContext->textureSize(WorkshopPreviewPlaceholder));
    guiContext->drawInterfaceQuad(WorkshopPreviewPlaceholder, position + (box - placeholderSize) / 2);
    return;
  }

  if (m_textureImage != m_image) {
    m_texture = guiContext->renderer()->createTexture(*m_image, TextureAddressing::Clamp, TextureFiltering::Linear);
    m_textureImage = m_image;
  }

  Vec2F imageSize = Vec2F(m_image->size());
  float scale = std::min(box[0] / imageSize[0], box[1] / imageSize[1]);
  Vec2F drawSize = imageSize * scale;
  RectF screenRect = RectF::withSize(position + (box - drawSize) / 2, drawSize).scaled(guiContext->interfaceScale());

  guiContext->renderer()->immediatePrimitives().emplace_back(std::in_place_type_t<RenderQuad>(), m_texture, screenRect, Vec4B::filled(255), 0.0f);
}

}
