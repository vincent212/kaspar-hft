/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include "actors/console/Table.hpp"
#include <sstream>

namespace frame {
namespace cons {

Table::Table(const std::string& title, const std::vector<std::string>& columns)
    : title_(title)
    , columns_(columns)
    , current_col_(0)
{
}

void Table::add_row() {
    rows_.emplace_back();
    current_col_ = 0;
}

std::string Table::to_json(bool pretty) const {
    nlohmann::json j;
    j["title"] = title_;
    j["columns"] = columns_;
    j["rows"] = rows_;

    if (pretty) {
        return j.dump(2);  // Indent with 2 spaces
    } else {
        return j.dump();
    }
}

std::string Table::to_string() const {
    std::ostringstream oss;

    // Header line: Title\tCol1\tCol2\t...
    oss << title_;
    for (const auto& col : columns_) {
        oss << '\t' << col;
    }
    oss << '\t' << '\n';

    // Data rows: \tval1\tval2\t...
    for (const auto& row : rows_) {
        oss << '\t';
        for (const auto& val : row) {
            if (val.is_string()) {
                oss << val.get<std::string>();
            } else if (val.is_number_integer()) {
                oss << val.get<int64_t>();
            } else if (val.is_number_float()) {
                oss << val.get<double>();
            } else if (val.is_boolean()) {
                oss << (val.get<bool>() ? "true" : "false");
            } else if (val.is_null()) {
                oss << "null";
            } else {
                oss << val.dump();
            }
            oss << '\t';
        }
        oss << '\n';
    }

    return oss.str();
}

} // namespace cons
} // namespace frame
