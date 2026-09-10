#pragma once

#include <atomic>
#include <string>

namespace hpv {

struct ImgurUploadResult {
    bool success = false;
    std::string url;           // Direct image link (i.imgur.com/{id}.png)
    std::string page_url;      // Imgur page link (imgur.com/{id})
    std::string deletehash;    // Deletion hash
    std::string image_id;      // Imgur image ID
    std::string thumbnail_url; // Medium thumbnail (320x320)
    std::string error;         // Error message if failed
    int http_status = 0;       // HTTP response code
};

ImgurUploadResult upload_to_imgur(const std::string& image_data,
                                  const std::string& client_id,
                                  std::atomic<float>* progress = nullptr);

}
