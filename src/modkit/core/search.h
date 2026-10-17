// Matching of the command palette's search box.
#pragma once

#include <string>

namespace mk {

// The match score of a query against a text: 0 = no match, higher is better. Every whitespace separated word of the query has to occur in the text
// (case-insensitive); a word at the start of the text or of a part of the name (after _ / or a space) and an early hit score more.
int search_score(const std::string& query, const std::string& text);

}  // namespace mk
