// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "core/layout/ng/ng_block_node.h"

#include "core/layout/LayoutBlockFlow.h"
#include "core/layout/api/LineLayoutAPIShim.h"
#include "core/layout/line/InlineIterator.h"
#include "core/layout/ng/layout_ng_block_flow.h"
#include "core/layout/ng/ng_block_layout_algorithm.h"
#include "core/layout/ng/ng_box_fragment.h"
#include "core/layout/ng/ng_constraint_space.h"
#include "core/layout/ng/ng_constraint_space_builder.h"
#include "core/layout/ng/ng_fragment_builder.h"
#include "core/layout/ng/ng_inline_node.h"
#include "core/layout/ng/ng_length_utils.h"
#include "core/layout/ng/ng_writing_mode.h"
#include "core/paint/PaintLayer.h"
#include "platform/RuntimeEnabledFeatures.h"

namespace blink {

namespace {

// Copies data back to the legacy layout tree for a given child fragment.
void FragmentPositionUpdated(const NGPhysicalBoxFragment& box_fragment) {
  LayoutBox* layout_box = toLayoutBox(box_fragment.GetLayoutObject());
  if (!layout_box)
    return;

  DCHECK(layout_box->parent()) << "Should be called on children only.";

  layout_box->setX(box_fragment.LeftOffset());
  layout_box->setY(box_fragment.TopOffset());
}

// Similar to FragmentPositionUpdated but for floats.
// - Updates layout object's geometric information.
// - Creates legacy FloatingObject and attached it to the provided parent.
void FloatingObjectPositionedUpdated(NGFloatingObject& floating_object,
                                     LayoutBox* parent) {
  NGPhysicalBoxFragment* box_fragment =
      toNGPhysicalBoxFragment(floating_object.fragment);
  FragmentPositionUpdated(*box_fragment);

  LayoutBox* layout_box = toLayoutBox(box_fragment->GetLayoutObject());
  DCHECK(layout_box->isFloating());

  if (parent && parent->isLayoutBlockFlow()) {
    FloatingObject* floating_object =
        toLayoutBlockFlow(parent)->insertFloatingObject(*layout_box);
    // TODO(glebl): Fix floating_object's inline offset if it's attached to
    // parent != layout_box_->parent
    floating_object->setX(box_fragment->LeftOffset());
    floating_object->setY(box_fragment->TopOffset());
    floating_object->setIsPlaced(true);
  }
}

}  // namespace

NGBlockNode::NGBlockNode(LayoutObject* layout_object)
    : NGLayoutInputNode(NGLayoutInputNodeType::kLegacyBlock),
      layout_box_(toLayoutBox(layout_object)) {
  DCHECK(layout_box_);
}

NGBlockNode::NGBlockNode(ComputedStyle* style)
    : NGLayoutInputNode(NGLayoutInputNodeType::kLegacyBlock),
      layout_box_(nullptr),
      style_(style) {
  DCHECK(style_);
}

// Need an explicit destructor in the .cc file, or the MSWIN compiler will
// produce an error when attempting to generate a default one, if the .h file is
// included from a compilation unit that lacks the ComputedStyle definition.
NGBlockNode::~NGBlockNode() {}

NGPhysicalFragment* NGBlockNode::Layout(NGConstraintSpace* constraint_space) {
  // We can either use the new layout code to do the layout and then copy the
  // resulting size to the LayoutObject, or use the old layout code and
  // synthesize a fragment.
  if (CanUseNewLayout()) {
    NGLayoutAlgorithm* algorithm =
        NGLayoutInputNode::AlgorithmForInputNode(this, constraint_space);
    fragment_ = toNGPhysicalBoxFragment(algorithm->Layout());
    CopyFragmentDataToLayoutBox(*constraint_space);
  } else {
    DCHECK(layout_box_);
    fragment_ = RunOldLayout(*constraint_space);
  }

  return fragment_;
}

MinAndMaxContentSizes NGBlockNode::ComputeMinAndMaxContentSizesSync() {
  MinAndMaxContentSizes sizes;
  while (!ComputeMinAndMaxContentSizes(&sizes))
    continue;
  return sizes;
}

bool NGBlockNode::ComputeMinAndMaxContentSizes(MinAndMaxContentSizes* sizes) {
  if (!CanUseNewLayout()) {
    DCHECK(layout_box_);
    // TODO(layout-ng): This could be somewhat optimized by directly calling
    // computeIntrinsicLogicalWidths, but that function is currently private.
    // Consider doing that if this becomes a performance issue.
    LayoutUnit borderAndPadding = layout_box_->borderAndPaddingLogicalWidth();
    sizes->min_content = layout_box_->computeLogicalWidthUsing(
                             MainOrPreferredSize, Length(MinContent),
                             LayoutUnit(), layout_box_->containingBlock()) -
                         borderAndPadding;
    sizes->max_content = layout_box_->computeLogicalWidthUsing(
                             MainOrPreferredSize, Length(MaxContent),
                             LayoutUnit(), layout_box_->containingBlock()) -
                         borderAndPadding;
    return true;
  }

  NGConstraintSpace* constraint_space =
      NGConstraintSpaceBuilder(
          FromPlatformWritingMode(Style()->getWritingMode()))
          .SetTextDirection(Style()->direction())
          .ToConstraintSpace();

  // TODO(cbiesinger): For orthogonal children, we need to always synthesize.
  NGBlockLayoutAlgorithm minmax_algorithm(
      layout_box_, Style(), toNGBlockNode(FirstChild()), constraint_space);
  if (minmax_algorithm.ComputeMinAndMaxContentSizes(sizes))
    return true;

  // Have to synthesize this value.
  NGPhysicalFragment* physical_fragment = Layout(constraint_space);
  NGBoxFragment* fragment = new NGBoxFragment(
      FromPlatformWritingMode(Style()->getWritingMode()), Style()->direction(),
      toNGPhysicalBoxFragment(physical_fragment));

  sizes->min_content = fragment->InlineOverflow();

  // Now, redo with infinite space for max_content
  constraint_space =
      NGConstraintSpaceBuilder(
          FromPlatformWritingMode(Style()->getWritingMode()))
          .SetTextDirection(Style()->direction())
          .SetAvailableSize({LayoutUnit::max(), LayoutUnit()})
          .SetPercentageResolutionSize({LayoutUnit(), LayoutUnit()})
          .ToConstraintSpace();

  physical_fragment = Layout(constraint_space);
  fragment = new NGBoxFragment(
      FromPlatformWritingMode(Style()->getWritingMode()), Style()->direction(),
      toNGPhysicalBoxFragment(physical_fragment));
  sizes->max_content = fragment->InlineOverflow();
  return true;
}

ComputedStyle* NGBlockNode::MutableStyle() {
  if (style_)
    return style_.get();
  DCHECK(layout_box_);
  return layout_box_->mutableStyle();
}

const ComputedStyle* NGBlockNode::Style() const {
  if (style_)
    return style_.get();
  DCHECK(layout_box_);
  return layout_box_->style();
}

NGBlockNode* NGBlockNode::NextSibling() {
  if (!next_sibling_) {
    LayoutObject* next_sibling =
        layout_box_ ? layout_box_->nextSibling() : nullptr;
    NGBlockNode* box = next_sibling ? new NGBlockNode(next_sibling) : nullptr;
    SetNextSibling(box);
  }
  return next_sibling_;
}

LayoutObject* NGBlockNode::GetLayoutObject() {
  return layout_box_;
}

NGLayoutInputNode* NGBlockNode::FirstChild() {
  if (!first_child_) {
    LayoutObject* child = layout_box_ ? layout_box_->slowFirstChild() : nullptr;
    if (child) {
      if (child->isInline()) {
        SetFirstChild(new NGInlineNode(child, MutableStyle()));
      } else {
        SetFirstChild(new NGBlockNode(child));
      }
    }
  }
  return first_child_;
}

void NGBlockNode::SetNextSibling(NGBlockNode* sibling) {
  next_sibling_ = sibling;
}

void NGBlockNode::SetFirstChild(NGLayoutInputNode* child) {
  first_child_ = child;
}

NGBreakToken* NGBlockNode::CurrentBreakToken() const {
  return fragment_ ? fragment_->BreakToken() : nullptr;
}

DEFINE_TRACE(NGBlockNode) {
  visitor->trace(fragment_);
  visitor->trace(next_sibling_);
  visitor->trace(first_child_);
  NGLayoutInputNode::trace(visitor);
}

bool NGBlockNode::CanUseNewLayout() {
  if (!layout_box_)
    return true;
  if (!layout_box_->isLayoutBlockFlow())
    return false;
  return RuntimeEnabledFeatures::layoutNGInlineEnabled() ||
         !HasInlineChildren();
}

bool NGBlockNode::HasInlineChildren() {
  if (!layout_box_ || !layout_box_->isLayoutBlockFlow())
    return false;

  const LayoutBlockFlow* block_flow = toLayoutBlockFlow(layout_box_);
  if (!block_flow->childrenInline())
    return false;
  LayoutObject* child = block_flow->firstChild();
  while (child) {
    if (child->isInline())
      return true;
    child = child->nextSibling();
  }

  return false;
}

void NGBlockNode::CopyFragmentDataToLayoutBox(
    const NGConstraintSpace& constraint_space) {
  // We may not have a layout_box_ during unit tests.
  if (!layout_box_)
    return;

  layout_box_->setWidth(fragment_->Width());
  layout_box_->setHeight(fragment_->Height());
  NGBoxStrut border_and_padding =
      ComputeBorders(*Style()) + ComputePadding(constraint_space, *Style());
  LayoutUnit intrinsic_logical_height =
      layout_box_->style()->isHorizontalWritingMode()
          ? fragment_->HeightOverflow()
          : fragment_->WidthOverflow();
  intrinsic_logical_height -= border_and_padding.BlockSum();
  layout_box_->setIntrinsicContentLogicalHeight(intrinsic_logical_height);

  // TODO(layout-dev): Currently we are not actually performing layout on
  // inline children. For now just clear the needsLayout bit so that we can
  // run unittests.
  if (HasInlineChildren()) {
    for (InlineWalker walker(
             LineLayoutBlockFlow(toLayoutBlockFlow(layout_box_)));
         !walker.atEnd(); walker.advance()) {
      LayoutObject* o = LineLayoutAPIShim::layoutObjectFrom(walker.current());
      o->clearNeedsLayout();
    }

    // Ensure the position of the children are copied across to the
    // LayoutObject tree.
  } else {
    for (const auto& child_fragment : fragment_->Children()) {
      if (child_fragment->IsPlaced())
        FragmentPositionUpdated(toNGPhysicalBoxFragment(*child_fragment));

      for (const auto& floating_object : child_fragment->PositionedFloats()) {
        FloatingObjectPositionedUpdated(*floating_object, layout_box_);
      }
    }
  }

  if (layout_box_->isLayoutBlock())
    toLayoutBlock(layout_box_)->layoutPositionedObjects(true);
  layout_box_->clearNeedsLayout();
  if (layout_box_->isLayoutBlockFlow()) {
    toLayoutBlockFlow(layout_box_)->updateIsSelfCollapsing();
  }
}

NGPhysicalBoxFragment* NGBlockNode::RunOldLayout(
    const NGConstraintSpace& constraint_space) {
  NGLogicalSize available_size = constraint_space.PercentageResolutionSize();
  layout_box_->setOverrideContainingBlockContentLogicalWidth(
      available_size.inline_size);
  layout_box_->setOverrideContainingBlockContentLogicalHeight(
      available_size.block_size);
  // TODO(layout-ng): Does this handle scrollbars correctly?
  if (constraint_space.IsFixedSizeInline()) {
    layout_box_->setOverrideLogicalContentWidth(
        constraint_space.AvailableSize().inline_size -
        layout_box_->borderAndPaddingLogicalWidth());
  }
  if (constraint_space.IsFixedSizeBlock()) {
    layout_box_->setOverrideLogicalContentHeight(
        constraint_space.AvailableSize().block_size -
        layout_box_->borderAndPaddingLogicalHeight());
  }

  if (layout_box_->isLayoutNGBlockFlow() && layout_box_->needsLayout()) {
    toLayoutNGBlockFlow(layout_box_)->LayoutBlockFlow::layoutBlock(true);
  } else {
    layout_box_->forceLayout();
  }
  LayoutRect overflow = layout_box_->layoutOverflowRect();
  // TODO(layout-ng): This does not handle writing modes correctly (for
  // overflow)
  NGFragmentBuilder builder(NGPhysicalFragment::kFragmentBox, layout_box_);
  builder.SetInlineSize(layout_box_->logicalWidth())
      .SetBlockSize(layout_box_->logicalHeight())
      .SetDirection(layout_box_->styleRef().direction())
      .SetWritingMode(
          FromPlatformWritingMode(layout_box_->styleRef().getWritingMode()))
      .SetInlineOverflow(overflow.width())
      .SetBlockOverflow(overflow.height());
  return builder.ToBoxFragment();
}

void NGBlockNode::UseOldOutOfFlowPositioning() {
  DCHECK(layout_box_);
  DCHECK(layout_box_->isOutOfFlowPositioned());
  layout_box_->containingBlock()->insertPositionedObject(layout_box_);
}

// Save static position for legacy AbsPos layout.
void NGBlockNode::SaveStaticOffsetForLegacy(const NGLogicalOffset& offset) {
  DCHECK(layout_box_);
  DCHECK(layout_box_->isOutOfFlowPositioned());
  DCHECK(layout_box_->layer());
  layout_box_->layer()->setStaticBlockPosition(offset.block_offset);
  layout_box_->layer()->setStaticInlinePosition(offset.inline_offset);
}

}  // namespace blink
