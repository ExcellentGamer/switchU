#include <math.h>
#include <cctype>
#include <string>
#include <nn/act.h>

#include "util.hpp"

std::string ACCOUNT_ID;

void get_user_information() {
    nn::act::Initialize();

    char account_id[256] = {};
    nn::act::GetAccountId(account_id);
    ACCOUNT_ID = account_id[0] ? std::string(account_id) : "User";
}

std::string sanitize_title_for_path(const std::string& title) {
    std::string sanitized = title;
    for (char& c : sanitized) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == ' ')) {
            c = '_';  // Replace anything not a-zA-Z0-9_-space with underscore
        }
    }
    return sanitized;
}
