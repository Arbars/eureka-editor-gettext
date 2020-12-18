//------------------------------------------------------------------------
//
//  Eureka DOOM Editor
//
//  Copyright (C) 2020 Ioan Chera
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

#include "m_strings.h"

#include "gtest/gtest.h"

TEST(StringTable, Test)
{
    StringTable table;
    int index = table.add("Jackson");
    ASSERT_EQ(table.get(index), "Jackson");
    int index2 = table.add("Jackson");
    ASSERT_EQ(index2, index);
    int index3 = table.add("Michael");
    ASSERT_NE(index3, index2);
    ASSERT_EQ(table.get(index3), "Michael");

    int index4 = table.add("jackson");
    ASSERT_NE(index4, index);
    ASSERT_EQ(table.get(index), "Jackson");
    ASSERT_EQ(table.get(index4), "jackson");
}
