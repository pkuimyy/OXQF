#pragma once

#include <oxq/core/game_model.hpp>

namespace oxq::format {

using core::Annotation;
using core::AnnotationKind;
using core::DatePrecision;
using core::EventMetadata;
using core::ExtensionMetadata;
using core::ExtensionProperties;
using core::ExtensionValue;
using core::GameMetadata;
using core::GameModel;
using core::GameResult;
using core::Move;
using core::MoveNode;
using core::MoveTree;
using core::NodeId;
using core::OpeningMetadata;
using core::Piece;
using core::PieceType;
using core::PlayerMetadata;
using core::Position;
using core::Provenance;
using core::Side;
using core::Uuid;

// The format-facing domain name. GameModel remains available during the 1.x
// source compatibility window; both names denote exactly the same type.
using GameDocument = core::GameModel;

}  // namespace oxq::format
