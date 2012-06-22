// Copyright 2012 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// Tests for BasicBlockSubGraph.

#include "syzygy/block_graph/basic_block_subgraph.h"

#include "gtest/gtest.h"
#include "syzygy/block_graph/basic_block.h"
#include "syzygy/block_graph/block_graph.h"
#include "syzygy/core/assembler.h"

namespace block_graph {

namespace {

using block_graph::BasicBlock;
using block_graph::BasicBlockReference;
using block_graph::BasicBlockReferrer;
using block_graph::BlockGraph;
using block_graph::Instruction;
using block_graph::Successor;

typedef BasicBlockSubGraph::BlockDescription BlockDescription;
typedef BlockGraph::Reference Reference;

// Some handy constants.
const size_t kDataSize = 32;
const uint8 kData[kDataSize] = {0};

// A derived class to expose protected members for unit-testing.
class TestBasicBlockSubGraph : public BasicBlockSubGraph {
 public:
   using BasicBlockSubGraph::HasValidReferrers;
   using BasicBlockSubGraph::HasValidSuccessors;
   using BasicBlockSubGraph::MapsBasicBlocksToAtMostOneDescription;
};

}  // namespace

TEST(BasicBlockSubGraph, AddBasicBlock) {
  BlockGraph::Block block;
  BasicBlockSubGraph subgraph;
  subgraph.set_original_block(&block);

  // Add a basic block.
  BasicBlock* bb1 = subgraph.AddBasicBlock(
      "bb1", BasicBlock::BASIC_CODE_BLOCK, 0, kDataSize, kData);
  ASSERT_FALSE(bb1 == NULL);

  // Cannot add one that overlaps.
  BasicBlock* bb2 = subgraph.AddBasicBlock(
      "bb2", BasicBlock::BASIC_CODE_BLOCK, kDataSize / 2, kDataSize, kData);
  ASSERT_TRUE(bb2 == NULL);

  // But can add one that doesn't overlap.
  BasicBlock* bb3 = subgraph.AddBasicBlock(
      "bb3", BasicBlock::BASIC_CODE_BLOCK, kDataSize, kDataSize, kData);
  ASSERT_FALSE(bb3 == NULL);

  // And they were not the same basic-block.
  ASSERT_FALSE(bb1 == bb3);
}

TEST(BasicBlockSubGraph, MapsBasicBlocksToAtMostOneDescription) {
  TestBasicBlockSubGraph subgraph;
  uint8 data[32] = {0};

  // Add three non-overlapping basic-blocks.
  BasicBlock* bb1 = subgraph.AddBasicBlock(
      "bb1", BasicBlock::BASIC_CODE_BLOCK, -1, 0, NULL);
  ASSERT_FALSE(bb1 == NULL);
  BasicBlock* bb2 = subgraph.AddBasicBlock(
      "bb2", BasicBlock::BASIC_CODE_BLOCK, -1, 0, NULL);
  ASSERT_FALSE(bb2 == NULL);
  BasicBlock* bb3 = subgraph.AddBasicBlock(
      "bb3", BasicBlock::BASIC_CODE_BLOCK, -1, 0, NULL);
  ASSERT_FALSE(bb3 == NULL);

  // They should all be different blocks.
  ASSERT_FALSE(bb1 == bb2);
  ASSERT_FALSE(bb2 == bb3);
  ASSERT_FALSE(bb1 == bb3);

  // Add a block description for a mythical b1.
  subgraph.block_descriptions().push_back(BlockDescription());
  BlockDescription& b1 = subgraph.block_descriptions().back();
  b1.type = BlockGraph::CODE_BLOCK;
  b1.name = "b1";
  b1.basic_block_order.push_back(bb1);

  // Add a block description for a mythical b2.
  subgraph.block_descriptions().push_back(BlockDescription());
  BlockDescription& b2 = subgraph.block_descriptions().back();
  b2.type = BlockGraph::CODE_BLOCK;
  b2.name = "b2";
  b2.basic_block_order.push_back(bb2);

  // There are no blocks assigned twice (bb1 and bb2 are in separate blocks).
  ASSERT_TRUE(subgraph.MapsBasicBlocksToAtMostOneDescription());

  // Adding bb3 to b1 is still valid.
  b1.basic_block_order.push_back(bb3);
  ASSERT_TRUE(subgraph.MapsBasicBlocksToAtMostOneDescription());

  // But adding bb3 to b2, as well, is no longer valid.
  b2.basic_block_order.push_back(bb3);
  ASSERT_FALSE(subgraph.MapsBasicBlocksToAtMostOneDescription());
}

TEST(BasicBlockSubGraph, DISABLED_HasValidSuccessors) {
  // TODO(rogerm): Enable this test when HasValidSuccessors is implemented.
  TestBasicBlockSubGraph subgraph;

  BasicBlock* bb1 = subgraph.AddBasicBlock(
      "bb1", BasicBlock::BASIC_CODE_BLOCK, -1, 0, NULL);
  ASSERT_FALSE(bb1 == NULL);

  BasicBlock* bb2 = subgraph.AddBasicBlock(
      "bb2", BasicBlock::BASIC_CODE_BLOCK, -1, 0, NULL);
  ASSERT_FALSE(bb2 == NULL);

  // Add a block description for a mythical b1.
  subgraph.block_descriptions().push_back(BlockDescription());
  BlockDescription& b1 = subgraph.block_descriptions().back();
  b1.type = BlockGraph::CODE_BLOCK;
  b1.name = "b1";
  b1.basic_block_order.push_back(bb1);

  // Add a block description for a mythical b2.
  subgraph.block_descriptions().push_back(BlockDescription());
  BlockDescription& b2 = subgraph.block_descriptions().back();
  b2.type = BlockGraph::CODE_BLOCK;
  b2.name = "b2";
  b2.basic_block_order.push_back(bb2);

  // Successors are not valid yet.
  EXPECT_FALSE(subgraph.HasValidSuccessors());

  // Add an unconditional succession from bb1 to bb2.
  bb1->successors().push_back(
      Successor(Successor::kConditionTrue,
                BasicBlockReference(BlockGraph::RELATIVE_REF, 4, bb2, 0, 0),
                -1, 0));

  // Successors are still not valid.
  EXPECT_FALSE(subgraph.HasValidSuccessors());

  // Add half of a conditional succession from bb2 to bb1.
  bb2->successors().push_back(
      Successor(Successor::kConditionAbove,
                BasicBlockReference(BlockGraph::RELATIVE_REF, 4, bb1, 0, 0),
                -1, 0));

  // Successors are still not valid.
  EXPECT_FALSE(subgraph.HasValidSuccessors());

  // Add second conditional succession from bb2 to bb1, but not the inverse
  // of the first condtition.
  bb2->successors().push_back(
      Successor(Successor::kConditionAboveOrEqual,
                BasicBlockReference(BlockGraph::RELATIVE_REF, 4, bb1, 0, 0),
                -1, 0));

  // Successors are still not valid because the conditions are not inverses.
  EXPECT_FALSE(subgraph.HasValidSuccessors());

  // Remove the bad successor and add a correct secondary successor.
  bb2->successors().pop_back();
  bb2->successors().push_back(
      Successor(Successor::kConditionBelowOrEqual,
                BasicBlockReference(BlockGraph::RELATIVE_REF, 4, bb1, 0, 0),
                -1, 0));

  // Successors are now valid.
  EXPECT_TRUE(subgraph.HasValidSuccessors());
}

TEST(BasicBlockSubGraph, HasValidReferrers) {
  BlockGraph::Block b1 = BlockGraph::Block(0, BlockGraph::DATA_BLOCK, 4, "b1");
  BlockGraph::Block b2 = BlockGraph::Block(0, BlockGraph::DATA_BLOCK, 4, "b2");

  Reference ref(BlockGraph::ABSOLUTE_REF, 4, &b1, 0, 0);
  ASSERT_TRUE(b2.SetReference(0, ref));
  ASSERT_FALSE(b1.referrers().empty());

  TestBasicBlockSubGraph subgraph;
  subgraph.set_original_block(&b1);

  ASSERT_FALSE(subgraph.HasValidReferrers());

  BasicBlock* bb1 = subgraph.AddBasicBlock(
      "bb1", BasicBlock::BASIC_DATA_BLOCK, -1, kDataSize, kData);
  ASSERT_FALSE(bb1 == NULL);

  subgraph.block_descriptions().push_back(BlockDescription());
  BlockDescription& b1_desc = subgraph.block_descriptions().back();
  b1_desc.name = b1.name();
  b1_desc.type = BlockGraph::DATA_BLOCK;
  b1_desc.basic_block_order.push_back(bb1);

  ASSERT_FALSE(subgraph.HasValidReferrers());

  bb1->referrers().insert(BasicBlockReferrer(&b2, 0));
  ASSERT_TRUE(subgraph.HasValidReferrers());
}

TEST(BasicBlockSubGraph, GetMaxSize) {
  TestBasicBlockSubGraph subgraph;

  // Add three non-overlapping basic-blocks.
  BasicBlock* code = subgraph.AddBasicBlock(
      "code", BasicBlock::BASIC_CODE_BLOCK, -1, 0, NULL);
  ASSERT_FALSE(code == NULL);
  BasicBlock* data = subgraph.AddBasicBlock(
      "data", BasicBlock::BASIC_DATA_BLOCK, -1, kDataSize / 2, kData);
  ASSERT_FALSE(data == NULL);
  BasicBlock* padding = subgraph.AddBasicBlock(
      "padding", BasicBlock::BASIC_PADDING_BLOCK, -1, kDataSize, kData);
  ASSERT_FALSE(padding == NULL);

  Instruction::Representation dummy;

  code->instructions().push_back(Instruction(dummy, -1, 5, kData));
  code->instructions().push_back(Instruction(dummy, -1, 1, kData));
  code->instructions().push_back(Instruction(dummy, -1, 3, kData));
  code->successors().push_back(Successor());
  code->successors().push_back(Successor());

  subgraph.block_descriptions().push_back(BlockDescription());
  BlockDescription& desc = subgraph.block_descriptions().back();
  desc.basic_block_order.push_back(code);
  desc.basic_block_order.push_back(data);
  desc.basic_block_order.push_back(padding);

  size_t max_block_length = kDataSize + (kDataSize / 2 ) + (5 + 1 + 3) +
      (2 * core::AssemblerImpl::kMaxInstructionLength);

  DCHECK_EQ(max_block_length, desc.GetMaxSize());
}

}  // namespace block_graph