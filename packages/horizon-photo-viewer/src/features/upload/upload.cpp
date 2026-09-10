#include "features/upload/upload.hpp"

#include <curl/curl.h>
#include <cstring>
#include <vector>

namespace hpv {

static const char kBase64Table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const std::string& in) {
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i < in.size()) {
        uint8_t b0 = (uint8_t)in[i++];
        uint8_t b1 = i < in.size() ? (uint8_t)in[i] : 0;
        uint8_t b2 = i + 1 < in.size() ? (uint8_t)in[i + 1] : 0;
        out += kBase64Table[b0 >> 2];
        out += kBase64Table[((b0 & 0x03) << 4) | (b1 >> 4)];
        if (i < in.size()) {
            out += kBase64Table[((b1 & 0x0F) << 2) | (b2 >> 6)];
            if (i + 1 < in.size()) {
                out += kBase64Table[b2 & 0x3F];
                i += 2;
            } else {
                out += '=';
                i += 1;
            }
        } else {
            out += '=';
            out += '=';
            i += 0;
        }
    }
    return out;
}

struct CurlWriteBuffer {
    std::string data;
};

static size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total = size * nmemb;
    auto* buf = static_cast<CurlWriteBuffer*>(userp);
    buf->data.append(static_cast<const char*>(contents), total);
    return total;
}

// Extract a JSON string value by key, searching at any nesting level.
// Handles escaped characters. Returns empty string if key not found or not a string.
static std::string json_extract_string(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return {};
    pos += search.size();
    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' ||
                                 json[pos] == '\n' || json[pos] == '\r'))
        pos++;
    if (pos >= json.size() || json[pos] != '"') return {};
    pos++; // skip opening quote
    std::string result;
    while (pos < json.size()) {
        if (json[pos] == '\\') {
            pos++;
            if (pos < json.size()) {
                result += json[pos];
                pos++;
            }
        } else if (json[pos] == '"') {
            break;
        } else {
            result += json[pos];
            pos++;
        }
    }
    return result;
}

// Extract true/false boolean value by key
static bool json_extract_bool(const std::string& json, const std::string& key, bool def) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
        pos++;
    if (json.substr(pos, 4) == "true") return true;
    if (json.substr(pos, 5) == "false") return false;
    return def;
}

// Extract error message from Imgur error response:
// {"data":{"error":"msg",...},"success":false,"status":400}
// The error field can be a string or an object: {"error":{"message":"msg",...}}
// Also handles HTML error pages (<title>...</title>)
static std::string parse_imgur_error(const std::string& body) {
    // Try JSON first
    std::string msg = json_extract_string(body, "message");
    if (!msg.empty()) return msg;
    msg = json_extract_string(body, "error");
    if (!msg.empty()) return msg;

    // Fallback: extract <title> from HTML error pages
    auto title_start = body.find("<title>");
    if (title_start != std::string::npos) {
        title_start += 7;
        auto title_end = body.find("</title>", title_start);
        if (title_end != std::string::npos) {
            return body.substr(title_start, title_end - title_start);
        }
    }

    return "Unknown error";
}

ImgurUploadResult upload_to_imgur(const std::string& image_data,
                                  const std::string& client_id,
                                  std::atomic<float>* progress) {
    ImgurUploadResult result;

    // Progress callback — writes 0.0–1.0 to *progress at each transfer step
    // Must use explicit function pointer: curl_easy_setopt is variadic, so implicit
    // stateless-lambda-to-function-pointer conversion does NOT happen for its args.
    int (*xferinfo)(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) =
        [](void* clientp, curl_off_t, curl_off_t,
           curl_off_t ultotal, curl_off_t ulnow) -> int {
            auto* p = static_cast<std::atomic<float>*>(clientp);
            if (ultotal > 0) {
                p->store((float)((double)ulnow / (double)ultotal), std::memory_order_relaxed);
            }
            return 0;
        };

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to initialize curl";
        return result;
    }

    std::string b64 = base64_encode(image_data);
    char* enc = curl_easy_escape(curl, b64.c_str(), (int)b64.size());
    std::string post_data = std::string("image=") + enc + "&type=base64";
    curl_free(enc);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    std::string auth = "Authorization: Client-ID " + client_id;
    headers = curl_slist_append(headers, auth.c_str());

    CurlWriteBuffer response;
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.imgur.com/3/image");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)post_data.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    // no timeout — uploads can take a while
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Horizon-Photo/1.0");

    if (progress) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, progress);
    }

    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.http_status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        result.error = curl_easy_strerror(res);
        return result;
    }

    if (result.http_status != 200) {
        std::string api_error = parse_imgur_error(response.data);
        result.error = "HTTP " + std::to_string(result.http_status) + ": " + api_error;
        return result;
    }

    // Extract fields from JSON response
    bool success = json_extract_bool(response.data, "success", false);
    if (!success) {
        result.error = parse_imgur_error(response.data);
        return result;
    }

    result.image_id = json_extract_string(response.data, "id");
    result.deletehash = json_extract_string(response.data, "deletehash");
    std::string link = json_extract_string(response.data, "link");

    if (link.empty()) {
        result.error = "No link in response";
        return result;
    }

    // Strip trailing '.' that Imgur sometimes appends
    if (!link.empty() && link.back() == '.')
        link.pop_back();

    result.url = link;
    result.page_url = "https://imgur.com/" + result.image_id;
    result.thumbnail_url = "https://i.imgur.com/" + result.image_id + "m.jpg"; // medium 320x320
    result.success = true;
    return result;
}

}
