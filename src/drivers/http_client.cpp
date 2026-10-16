#include "drivers/http_client.h"

#include <algorithm>

const std::string* HttpResponse::header(const std::string& name) const {
    const auto found = std::find_if(headers.begin(), headers.end(),
                                    [&name](const std::pair<std::string, std::string>& entry) { return entry.first == name; });
    if (found == headers.end()) {
        return nullptr;
    }
    return &found->second;
}
