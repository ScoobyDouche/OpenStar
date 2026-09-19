#pragma once

#include "StarWidget.hpp"
#include "StarRenderer.hpp"
#include "StarHttpClient.hpp"
#include "StarImage.hpp"

namespace Star {

STAR_CLASS(WorkshopPreviewCache);
STAR_CLASS(WorkshopPreviewWidget);

// Fetches, decodes and caches Workshop preview images in memory.
class WorkshopPreviewCache {
public:
  static size_t const MaxEntries = 64;
  static unsigned const MaxImageSide = 512;

  // Returns the preview if it is ready.  The first call for an item starts
  // fetching it, provided the URL is on an allowed host.
  ImageConstPtr get(String const& itemId, String const& url);

  // Decodes any finished fetches.  Call once per frame.
  void update();

private:
  struct Entry {
    Maybe<WorkerPoolPromise<HttpResponse>> fetch;
    ImageConstPtr image;
    uint64_t lastUsed = 0;
  };

  void evictOldest();

  HashMap<String, Entry> m_entries;
  uint64_t m_useCounter = 0;
};

// Draws an in-memory image centered and aspect-fitted in the widget bounds,
// or a placeholder icon when there is no image.
class WorkshopPreviewWidget : public Widget {
public:
  WorkshopPreviewWidget();

  void setImage(ImageConstPtr image);

protected:
  void renderImpl() override;

private:
  ImageConstPtr m_image;
  ImageConstPtr m_textureImage;
  TexturePtr m_texture;
};

}
