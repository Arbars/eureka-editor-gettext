//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2021 Ioan Chera
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 2
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//------------------------------------------------------------------------

#include "m_parse.h"
#include "m_strings.h"
#include "gtest/gtest.h"

TEST(MParse, MParseLine)
{
    SString line;
    std::vector<SString> tokens;

    static const ParseOptions allOptions[] =
    {
        ParseOptions::noStrings,
        ParseOptions::haveStrings,
        ParseOptions::haveStringsKeepQuotes
    };

    // Common option tests
    for(ParseOptions option : allOptions)
    {
        // Test for empty lines
        ASSERT_EQ(M_ParseLine("", tokens, option), 0);
        ASSERT_EQ(M_ParseLine(" ", tokens, option), 0);
        ASSERT_EQ(M_ParseLine("    \t  ", tokens, option), 0);

        // Comments shouldn't count
        ASSERT_EQ(M_ParseLine("\t\t#Hello comment", tokens, option), 0);
        ASSERT_EQ(M_ParseLine("    # Hello comment", tokens, option), 0);

        // Newline should end the parsing
        ASSERT_EQ(M_ParseLine(" # Hello comment \n Hello next line", tokens,
							  option), 0);
        ASSERT_EQ(M_ParseLine(" # Hello comment \r\n Hello next line", tokens,
							  option), 0);

        // Except if it's after an empty line, then we just go ahead
        ASSERT_EQ(M_ParseLine("\n Hello next line", tokens, option), 3);
        ASSERT_EQ(tokens[0], "Hello");
        ASSERT_EQ(tokens[1], "next");
        ASSERT_EQ(tokens[2], "line");

        // Now we have something going on. The comment and next line
        ASSERT_EQ(M_ParseLine(" Fir+st, .line \r\n Hello next line", tokens, option), 2);

        // Check that operators become part of tokens
        ASSERT_EQ(tokens[0], "Fir+st,");
        ASSERT_EQ(tokens[1], ".line");

        // Check that no crash happens if last line is newline AND that comments can only be on
        // start of line
        ASSERT_EQ(M_ParseLine(" Trailed line #here\n", tokens, option), 3);
        ASSERT_EQ(tokens[0], "Trailed");
        ASSERT_EQ(tokens[1], "line");
        ASSERT_EQ(tokens[2], "#here");

        // Same if spaced apart
        ASSERT_EQ(M_ParseLine(" Trailed line # here\n", tokens, option), 4);
        ASSERT_EQ(tokens[0], "Trailed");
        ASSERT_EQ(tokens[1], "line");
        ASSERT_EQ(tokens[2], "#");
        ASSERT_EQ(tokens[3], "here");
    }

    // Check haveStrings

    static const ParseOptions stringOptions[] =
    {
        ParseOptions::haveStrings,
        ParseOptions::haveStringsKeepQuotes
    };

    auto assertQuoted = [](const SString &token, const char *value, ParseOptions option)
    {
        if(option == ParseOptions::haveStrings)
            ASSERT_EQ(token, value);
        else    // haveStringsKeepQuotes
            ASSERT_EQ(token, SString("\"") + value + "\"");
    };

    // Check unterminated string
    for(ParseOptions option : stringOptions)
    {
        // Check that unterminated quote is illegal
        ASSERT_EQ(M_ParseLine("Hello unterminated \"quote here", tokens, option), ParseLine_stringError);
        // Check that simple quote is illegal
        ASSERT_EQ(M_ParseLine("\"", tokens, option), ParseLine_stringError);
        // Check that newline in string is illegal
        ASSERT_EQ(M_ParseLine("Hello newline \"string\n here\"", tokens, option), ParseLine_stringError);

        // Now check string
        ASSERT_EQ(M_ParseLine("Hello, \"multi word string\" here!", tokens, option), 3);
        ASSERT_EQ(tokens[0], "Hello,");
        assertQuoted(tokens[1], "multi word string", option);
        ASSERT_EQ(tokens[2], "here!");

        // Check that string starting with # is legal
        ASSERT_EQ(M_ParseLine("\"#multi word\" here.", tokens, option), 2);
        assertQuoted(tokens[0], "#multi word", option);
        ASSERT_EQ(tokens[1], "here.");

        // Check that empty string is a token in itself
        ASSERT_EQ(M_ParseLine("Here's \"\" empty string", tokens, option), 4);
        ASSERT_EQ(tokens[0], "Here's");
        assertQuoted(tokens[1], "", option);
        ASSERT_EQ(tokens[2], "empty");
        ASSERT_EQ(tokens[3], "string");

        // Check that touching strings is legal
        ASSERT_EQ(M_ParseLine("\"three\"\"\"\"strings\"", tokens, option), 3);
        assertQuoted(tokens[0], "three", option);
        assertQuoted(tokens[1], "", option);
        assertQuoted(tokens[2], "strings", option);

        // Check that just three """ is illegal
        ASSERT_EQ(M_ParseLine("\"\"\"", tokens, option), ParseLine_stringError);
    }
}
