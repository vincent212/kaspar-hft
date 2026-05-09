#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 * Contact: v@m2te.ch | https://www.linkedin.com/in/vmayeski/
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <vector>
#include <any>
#include <sstream>
#include <nlohmann/json.hpp>

namespace frame {
namespace cons {

/**
 * Table - Data table container for monitoring responses
 *
 * Supports both modern JSON format and legacy tab-delimited format.
 * Matches Python Table API for cross-language compatibility.
 *
 * Example:
 *   Table table("Actors", {"Name", "Type", "Queue Size"});
 *   table.add_row();
 *   table.set_value("Actor1");
 *   table.set_value("TestActor");
 *   table.set_value(0);
 *   std::string json = table.to_json(true);
 */
class Table {
public:
    /**
     * Create a new table with title and column names.
     *
     * @param title Table title
     * @param columns Column names
     */
    Table(const std::string& title, const std::vector<std::string>& columns);

    /**
     * Add a new row to the table.
     * Resets column counter to 0.
     */
    void add_row();

    /**
     * Set value in current column of current row.
     * Advances column counter.
     *
     * @param val Value to set (string, int, double, bool, etc.)
     */
    template<typename T>
    void set_value(const T& val);

    /**
     * Serialize table to JSON format.
     *
     * Format:
     * {
     *   "title": "Table Title",
     *   "columns": ["Col1", "Col2"],
     *   "rows": [
     *     ["val1", "val2"],
     *     ["val3", "val4"]
     *   ]
     * }
     *
     * @param pretty If true, pretty-print with indentation
     * @return JSON string
     */
    std::string to_json(bool pretty = true) const;

    /**
     * Serialize table to legacy tab-delimited format.
     *
     * Format:
     * Title\tCol1\tCol2\t
     * \tval1\tval2\t
     * \tval3\tval4\t
     *
     * @return Tab-delimited string
     */
    std::string to_string() const;

    // Accessors
    const std::string& get_title() const { return title_; }
    const std::vector<std::string>& get_columns() const { return columns_; }
    size_t row_count() const { return rows_.size(); }

private:
    std::string title_;
    std::vector<std::string> columns_;
    std::vector<std::vector<nlohmann::json>> rows_;
    size_t current_col_ = 0;
};

// Template implementation
template<typename T>
void Table::set_value(const T& val) {
    if (!rows_.empty()) {
        rows_.back().push_back(val);
        current_col_++;
    }
}

} // namespace cons
} // namespace frame
