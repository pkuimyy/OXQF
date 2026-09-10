#include <oxq/editor/editor_session.hpp>

#include <oxq/format/state_validation.hpp>
#include <oxq/format/validation.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace oxq::editor {
namespace {

[[nodiscard]] format::Side opposite(format::Side side) noexcept {
  return side == format::Side::red ? format::Side::black : format::Side::red;
}

[[nodiscard]] EditorError node_error(format::NodeId node) {
  EditorError error;
  error.code = EditorErrorCode::node_not_found;
  error.message = "node " + std::to_string(node) + " does not exist";
  error.node_id = node;
  return error;
}

[[nodiscard]] EditorError position_error(format::NodeId node,
                                         std::string message) {
  EditorError error;
  error.code = EditorErrorCode::invalid_position;
  error.message = std::move(message);
  error.node_id = node;
  return error;
}

[[nodiscard]] EditorError revision_error(std::uint64_t expected,
                                         std::uint64_t actual,
                                         std::string message) {
  EditorError error;
  error.code = EditorErrorCode::revision_conflict;
  error.message = std::move(message);
  error.expected_revision = expected;
  error.actual_revision = actual;
  return error;
}

[[nodiscard]] std::vector<format::ValidationIssue> validate_document(
    const format::GameDocument& document, const SessionOptions& options) {
  return options.validation_policy == ValidationPolicy::structural
             ? format::validate(document)
             : format::validate_state(document);
}

[[nodiscard]] bool reorder_child(format::GameDocument& document,
                                 format::NodeId parent_id,
                                 format::NodeId node_id,
                                 std::size_t target_index) {
  auto* parent = document.move_tree.findNode(parent_id);
  if (parent == nullptr || target_index >= parent->children.size()) {
    return false;
  }
  const auto current = std::ranges::find(parent->children, node_id);
  if (current == parent->children.end()) {
    return false;
  }
  const auto node = *current;
  parent->children.erase(current);
  parent->children.insert(
      parent->children.begin() + static_cast<std::ptrdiff_t>(target_index), node);
  return document.move_tree.validateInvariants();
}

void append_unique(std::vector<format::NodeId>& target,
                   const std::vector<format::NodeId>& source) {
  for (const auto node : source) {
    if (std::ranges::find(target, node) == target.end()) {
      target.push_back(node);
    }
  }
}

void merge_changes(ChangeSet& target, const ChangeSet& source) {
  append_unique(target.inserted, source.inserted);
  append_unique(target.removed, source.removed);
  append_unique(target.updated, source.updated);
  append_unique(target.reordered_parents, source.reordered_parents);
  target.metadata_changed = target.metadata_changed || source.metadata_changed;
  target.selection_changed = target.selection_changed || source.selection_changed;
}

[[nodiscard]] std::size_t string_bytes(const std::string& value) noexcept {
  return value.capacity();
}

[[nodiscard]] std::size_t optional_string_bytes(
    const std::optional<std::string>& value) noexcept {
  return value.has_value() ? string_bytes(*value) : 0;
}

[[nodiscard]] std::size_t annotation_bytes(
    const format::Annotation& annotation) noexcept {
  return sizeof(format::Annotation) + string_bytes(annotation.text) +
         optional_string_bytes(annotation.author) +
         optional_string_bytes(annotation.language);
}

[[nodiscard]] std::size_t metadata_bytes(
    const format::GameMetadata& metadata) noexcept {
  std::size_t bytes = sizeof(format::GameMetadata);
  const auto add_player = [&](const format::PlayerMetadata& player) {
    bytes += optional_string_bytes(player.name) + optional_string_bytes(player.id) +
             optional_string_bytes(player.country) +
             optional_string_bytes(player.title) + optional_string_bytes(player.team) +
             optional_string_bytes(player.time_used);
  };
  add_player(metadata.red_player);
  add_player(metadata.black_player);
  bytes += optional_string_bytes(metadata.event.name) +
           optional_string_bytes(metadata.event.id) +
           optional_string_bytes(metadata.event.location) +
           optional_string_bytes(metadata.event.organizer) +
           optional_string_bytes(metadata.event.round) +
           optional_string_bytes(metadata.event.type) +
           optional_string_bytes(metadata.event.group) +
           optional_string_bytes(metadata.event.board_number) +
           optional_string_bytes(metadata.event.time_control) +
           optional_string_bytes(metadata.event.start_time) +
           optional_string_bytes(metadata.event.end_time) +
           optional_string_bytes(metadata.result_text) +
           optional_string_bytes(metadata.opening.name) +
           optional_string_bytes(metadata.opening.code) +
           optional_string_bytes(metadata.opening.id) +
           optional_string_bytes(metadata.title) +
           optional_string_bytes(metadata.game_type) +
           optional_string_bytes(metadata.referee) +
           optional_string_bytes(metadata.recorder) +
           optional_string_bytes(metadata.commentator) +
           optional_string_bytes(metadata.commentator_uri) +
           optional_string_bytes(metadata.creator) +
           optional_string_bytes(metadata.creator_uri) +
           optional_string_bytes(metadata.record_created_at) +
           optional_string_bytes(metadata.record_modified_at) +
           optional_string_bytes(metadata.provenance.source_format) +
           optional_string_bytes(metadata.provenance.source_record_id) +
           optional_string_bytes(metadata.provenance.source_uri) +
           optional_string_bytes(metadata.provenance.import_note) +
           optional_string_bytes(metadata.provenance.source_format_version) +
           optional_string_bytes(metadata.provenance.source_library_id) +
           optional_string_bytes(metadata.provenance.source_library_name) +
           optional_string_bytes(metadata.provenance.source_category);
  bytes += metadata.tags.capacity() * sizeof(std::string);
  for (const auto& tag : metadata.tags) {
    bytes += string_bytes(tag);
  }
  for (const auto& [name, properties] : metadata.extensions) {
    bytes += sizeof(name) + string_bytes(name) + sizeof(properties);
    for (const auto& [key, value] : properties) {
      bytes += sizeof(key) + string_bytes(key) + sizeof(value);
      std::visit(
          [&](const auto& stored) {
            using Value = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<Value, std::string>) {
              bytes += string_bytes(stored);
            } else {
              bytes += stored.capacity() * sizeof(std::string);
              for (const auto& item : stored) {
                bytes += string_bytes(item);
              }
            }
          },
          value);
    }
  }
  return bytes;
}

[[nodiscard]] std::size_t node_dynamic_bytes(
    const format::MoveNode& node) noexcept {
  std::size_t bytes = node.children.capacity() * sizeof(format::NodeId) +
                      node.annotations.capacity() * sizeof(format::Annotation);
  for (const auto& annotation : node.annotations) {
    bytes += annotation_bytes(annotation) - sizeof(format::Annotation);
  }
  return bytes;
}

[[nodiscard]] std::size_t document_bytes(
    const format::GameDocument& document) noexcept {
  std::size_t bytes = sizeof(format::GameDocument) + metadata_bytes(document.metadata) +
                      document.initial_position.pieces.capacity() *
                          sizeof(format::Piece) +
                      document.move_tree.nodes.capacity() * sizeof(format::MoveNode);
  for (const auto& node : document.move_tree.nodes) {
    bytes += node_dynamic_bytes(node);
  }
  return bytes;
}

[[nodiscard]] std::variant<format::Position, EditorError> compute_position(
    const format::GameDocument& document, format::NodeId target) {
  std::unordered_map<format::NodeId, const format::MoveNode*> nodes;
  nodes.reserve(document.move_tree.nodes.size());
  for (const auto& node : document.move_tree.nodes) {
    nodes.emplace(node.id, &node);
  }

  const auto target_iterator = nodes.find(target);
  if (target_iterator == nodes.end()) {
    return node_error(target);
  }

  std::vector<const format::MoveNode*> path;
  const format::MoveNode* node = target_iterator->second;
  while (node->parent.has_value()) {
    path.push_back(node);
    const auto parent = nodes.find(*node->parent);
    if (parent == nodes.end()) {
      return position_error(node->id, "node parent does not exist");
    }
    node = parent->second;
  }
  std::ranges::reverse(path);

  std::array<std::optional<format::Piece>, 90> board;
  for (const auto& piece : document.initial_position.pieces) {
    board[piece.square] = piece;
  }

  format::Side side_to_move = document.initial_position.side_to_move;
  std::uint16_t fullmove_number = document.initial_position.fullmove_number;
  for (const auto* move_node : path) {
    if (!move_node->move.has_value()) {
      return position_error(move_node->id, "non-root node has no move");
    }
    const auto move = *move_node->move;
    if (!board[move.from_square].has_value()) {
      return position_error(move_node->id, "move origin does not contain a piece");
    }
    auto moving = *board[move.from_square];
    if (moving.side != side_to_move) {
      return position_error(move_node->id, "move origin belongs to the other side");
    }
    if (board[move.to_square].has_value() &&
        board[move.to_square]->side == moving.side) {
      return position_error(move_node->id, "move destination contains a friendly piece");
    }
    moving.square = move.to_square;
    board[move.to_square] = moving;
    board[move.from_square].reset();
    if (side_to_move == format::Side::black) {
      if (fullmove_number == std::numeric_limits<std::uint16_t>::max()) {
        EditorError error;
        error.code = EditorErrorCode::move_number_overflow;
        error.message = "fullmove number exceeds the format position range";
        error.node_id = move_node->id;
        return error;
      }
      ++fullmove_number;
    }
    side_to_move = opposite(side_to_move);
  }

  format::Position position;
  position.side_to_move = side_to_move;
  position.fullmove_number = fullmove_number;
  for (const auto& square : board) {
    if (square.has_value()) {
      position.pieces.push_back(*square);
    }
  }
  return position;
}

}  // namespace

EditorSession::EditorSession(format::GameDocument document)
    : document_(std::move(document)) {}

const format::GameDocument& EditorSession::document() const noexcept {
  return document_;
}

format::GameDocument EditorSession::export_document() const {
  return document_;
}

const SessionState& EditorSession::state() const noexcept {
  return state_;
}

SnapshotOutcome EditorSession::snapshot() const {
  auto position = position_at(state_.current_node);
  if (std::holds_alternative<EditorError>(position)) {
    return std::get<EditorError>(std::move(position));
  }
  return SessionSnapshot{state_, std::get<format::Position>(std::move(position))};
}

std::variant<format::Position, EditorError> EditorSession::position_at(
    format::NodeId node) const {
  const auto cached = position_cache_.find(node);
  if (cached != position_cache_.end()) {
    ++position_cache_hits_;
    return cached->second;
  }

  ++position_cache_misses_;
  auto position = compute_position(document_, node);
  if (std::holds_alternative<EditorError>(position) ||
      options_.max_position_cache_entries == 0) {
    return position;
  }
  try {
    if (position_cache_.size() >= options_.max_position_cache_entries) {
      position_cache_.clear();
    }
    position_cache_.insert_or_assign(
        node, std::get<format::Position>(position));
  } catch (const std::bad_alloc&) {
    position_cache_.clear();
  }
  return position;
}

PositionCacheStats EditorSession::position_cache_stats() const noexcept {
  return PositionCacheStats{position_cache_.size(), position_cache_hits_,
                            position_cache_misses_};
}

void EditorSession::invalidate_position_cache(
    const std::vector<format::NodeId>& nodes) {
  for (const auto node : nodes) {
    position_cache_.erase(node);
  }
}

void EditorSession::invalidate_position_subtree(format::NodeId root) {
  for (auto iterator = position_cache_.begin();
       iterator != position_cache_.end();) {
    auto current = iterator->first;
    bool affected = false;
    while (true) {
      if (current == root) {
        affected = true;
        break;
      }
      const auto* node = document_.move_tree.findNode(current);
      if (node == nullptr || !node->parent.has_value()) {
        break;
      }
      current = *node->parent;
    }
    if (affected) {
      iterator = position_cache_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

bool EditorSession::restore_subtree(format::GameDocument& document,
                                    const HistoryEntry& entry) const {
  auto& storage = document.move_tree.nodes;
  for (const auto& stored : entry.nodes) {
    if (stored.storage_index > storage.size()) {
      return false;
    }
    storage.insert(storage.begin() + static_cast<std::ptrdiff_t>(stored.storage_index),
                   stored.node);
  }
  if (!document.move_tree.rebuildIndex()) {
    return false;
  }
  auto* parent = document.move_tree.findNode(entry.parent);
  if (parent == nullptr || entry.sibling_index > parent->children.size()) {
    return false;
  }
  parent->children.insert(
      parent->children.begin() + static_cast<std::ptrdiff_t>(entry.sibling_index),
      entry.root);
  return document.move_tree.validateInvariants();
}

std::size_t EditorSession::estimate_history_bytes(
    const HistoryEntry& entry) const noexcept {
  std::size_t bytes = sizeof(HistoryEntry) +
                      entry.nodes.capacity() * sizeof(StoredNode);
  for (const auto& stored : entry.nodes) {
    bytes += node_dynamic_bytes(stored.node);
  }
  const auto add_annotations = [&](const auto& annotations) {
    if (!annotations.has_value()) {
      return;
    }
    bytes += annotations->capacity() * sizeof(format::Annotation);
    for (const auto& annotation : *annotations) {
      bytes += annotation_bytes(annotation) - sizeof(format::Annotation);
    }
  };
  add_annotations(entry.before_annotations);
  add_annotations(entry.after_annotations);
  if (entry.before_metadata.has_value()) {
    bytes += metadata_bytes(*entry.before_metadata);
  }
  if (entry.after_metadata.has_value()) {
    bytes += metadata_bytes(*entry.after_metadata);
  }
  if (entry.before_document.has_value()) {
    bytes += document_bytes(*entry.before_document);
  }
  if (entry.after_document.has_value()) {
    bytes += document_bytes(*entry.after_document);
  }
  if (entry.forward_changes.has_value()) {
    const auto& changes = *entry.forward_changes;
    bytes += sizeof(ChangeSet) +
             changes.inserted.capacity() * sizeof(format::NodeId) +
             changes.removed.capacity() * sizeof(format::NodeId) +
             changes.updated.capacity() * sizeof(format::NodeId) +
             changes.reordered_parents.capacity() * sizeof(format::NodeId);
  }
  return bytes;
}

Status EditorSession::store_history(HistoryEntry entry) {
  const auto entry_bytes = estimate_history_bytes(entry);
  if (options_.max_history_entries == 0 ||
      entry_bytes > options_.max_history_bytes) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "editor history entry exceeds the configured limits";
    return error;
  }

  try {
    auto next_history = undo_history_;
    next_history.push_back(std::move(entry));
    std::size_t total_bytes = 0;
    for (const auto& stored : next_history) {
      total_bytes += estimate_history_bytes(stored);
    }
    while (next_history.size() > 1 &&
           (next_history.size() > options_.max_history_entries ||
            total_bytes > options_.max_history_bytes)) {
      total_bytes -= estimate_history_bytes(next_history.front());
      next_history.erase(next_history.begin());
    }
    undo_history_.swap(next_history);
    redo_history_.clear();
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to store editor history";
    return error;
  }
  return std::nullopt;
}

CommandOutcome EditorSession::execute(
    Command command, std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }

  if (std::holds_alternative<CompoundCommand>(command)) {
    const auto& compound = std::get<CompoundCommand>(command);
    if (compound.commands.size() > options_.max_compound_commands) {
      EditorError error;
      error.code = EditorErrorCode::resource_limit;
      error.message = "compound command exceeds the configured command limit";
      return error;
    }
    if (compound.commands.empty()) {
      ChangeSet changes;
      changes.before_revision = state_.revision;
      changes.after_revision = state_.revision;
      return CommandResult{state_.revision, std::move(changes), std::nullopt};
    }

    EditorSession temporary{document_};
    temporary.state_ = state_;
    temporary.options_ = options_;
    temporary.options_.max_history_entries =
        std::numeric_limits<std::size_t>::max();
    temporary.options_.max_history_bytes =
        std::numeric_limits<std::size_t>::max();
    temporary.current_document_token_ = current_document_token_;
    temporary.saved_document_token_ = saved_document_token_;
    temporary.next_document_token_ = next_document_token_;

    ChangeSet merged;
    for (const auto& atomic : compound.commands) {
      Command child = std::visit(
          [](const auto& value) -> Command { return value; }, atomic);
      auto outcome = temporary.execute(std::move(child), temporary.state_.revision);
      if (std::holds_alternative<EditorError>(outcome)) {
        return std::get<EditorError>(std::move(outcome));
      }
      merge_changes(merged, std::get<CommandResult>(outcome).changes);
    }

    if (temporary.document_ == document_) {
      ChangeSet changes;
      changes.before_revision = state_.revision;
      changes.after_revision = state_.revision;
      return CommandResult{state_.revision, std::move(changes), std::nullopt};
    }

    const auto before_revision = state_.revision;
    const auto before_current = state_.current_node;
    const auto after_current = temporary.state_.current_node;
    const auto before_token = current_document_token_;
    const auto after_token = next_document_token_;

    HistoryEntry history;
    history.kind = HistoryKind::compound;
    history.before_current = before_current;
    history.after_current = after_current;
    history.before_token = before_token;
    history.after_token = after_token;
    history.before_document = document_;
    history.after_document = temporary.document_;
    history.forward_changes = merged;
    if (auto error = store_history(std::move(history)); error.has_value()) {
      return *std::move(error);
    }

    document_ = std::move(temporary.document_);
    position_cache_.clear();
    state_.current_node = after_current;
    state_.revision = before_revision + 1;
    current_document_token_ = after_token;
    ++next_document_token_;
    state_.dirty = current_document_token_ != saved_document_token_;
    state_.can_undo = true;
    state_.can_redo = false;

    merged.before_revision = before_revision;
    merged.after_revision = state_.revision;
    merged.selection_changed = before_current != after_current;
    return CommandResult{state_.revision, std::move(merged), std::nullopt};
  }

  if (std::holds_alternative<SetMetadataCommand>(command)) {
    const auto& update = std::get<SetMetadataCommand>(command);
    if (document_.metadata == update.metadata) {
      ChangeSet changes;
      changes.before_revision = state_.revision;
      changes.after_revision = state_.revision;
      return CommandResult{state_.revision, std::move(changes), std::nullopt};
    }

    format::GameDocument working = document_;
    const auto previous = working.metadata;
    working.metadata = update.metadata;
    auto issues = validate_document(working, options_);
    if (format::has_errors(issues)) {
      EditorError error;
      error.code = EditorErrorCode::validation_failed;
      error.message = "updated metadata failed document validation";
      error.validation_issues = std::move(issues);
      return error;
    }

    const auto before_revision = state_.revision;
    const auto before_token = current_document_token_;
    const auto after_token = next_document_token_;
    HistoryEntry history;
    history.kind = HistoryKind::set_metadata;
    history.before_current = state_.current_node;
    history.after_current = state_.current_node;
    history.before_token = before_token;
    history.after_token = after_token;
    history.before_metadata = previous;
    history.after_metadata = update.metadata;
    if (auto error = store_history(std::move(history)); error.has_value()) {
      return *std::move(error);
    }
    document_ = std::move(working);
    ++state_.revision;
    current_document_token_ = after_token;
    ++next_document_token_;
    state_.dirty = current_document_token_ != saved_document_token_;
    state_.can_undo = true;
    state_.can_redo = false;

    ChangeSet changes;
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
    changes.metadata_changed = true;
    return CommandResult{state_.revision, std::move(changes), std::nullopt};
  }

  if (std::holds_alternative<SetAnnotationsCommand>(command)) {
    const auto& update = std::get<SetAnnotationsCommand>(command);
    const auto* target = document_.move_tree.findNode(update.node);
    if (target == nullptr) {
      return node_error(update.node);
    }
    if (target->annotations == update.annotations) {
      ChangeSet changes;
      changes.before_revision = state_.revision;
      changes.after_revision = state_.revision;
      return CommandResult{state_.revision, std::move(changes), std::nullopt};
    }

    const auto previous = target->annotations;
    format::GameDocument working = document_;
    auto* working_target = working.move_tree.findNode(update.node);
    if (working_target == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "annotation target disappeared from the working document";
      error.node_id = update.node;
      return error;
    }
    working_target->annotations = update.annotations;
    auto issues = validate_document(working, options_);
    if (format::has_errors(issues)) {
      EditorError error;
      error.code = EditorErrorCode::validation_failed;
      error.message = "updated annotations failed document validation";
      error.validation_issues = std::move(issues);
      error.node_id = update.node;
      return error;
    }

    const auto before_revision = state_.revision;
    const auto before_token = current_document_token_;
    const auto after_token = next_document_token_;
    HistoryEntry history;
    history.kind = HistoryKind::set_annotations;
    history.root = update.node;
    history.before_current = state_.current_node;
    history.after_current = state_.current_node;
    history.before_token = before_token;
    history.after_token = after_token;
    history.before_annotations = previous;
    history.after_annotations = update.annotations;
    if (auto error = store_history(std::move(history)); error.has_value()) {
      return *std::move(error);
    }
    document_ = std::move(working);
    ++state_.revision;
    current_document_token_ = after_token;
    ++next_document_token_;
    state_.dirty = current_document_token_ != saved_document_token_;
    state_.can_undo = true;
    state_.can_redo = false;

    ChangeSet changes;
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
    changes.updated = {update.node};
    return CommandResult{state_.revision, std::move(changes), std::nullopt};
  }

  if (std::holds_alternative<ReorderVariationCommand>(command)) {
    const auto& reorder = std::get<ReorderVariationCommand>(command);
    if (reorder.node == 0) {
      EditorError error;
      error.code = EditorErrorCode::invalid_argument;
      error.message = "the root node has no sibling position";
      error.node_id = reorder.node;
      return error;
    }
    const auto* target = document_.move_tree.findNode(reorder.node);
    if (target == nullptr) {
      return node_error(reorder.node);
    }
    const auto parent_id = *target->parent;
    const auto* parent = document_.move_tree.findNode(parent_id);
    if (parent == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "reordered variation has no valid parent";
      error.node_id = reorder.node;
      return error;
    }
    if (reorder.target_index >= parent->children.size()) {
      EditorError error;
      error.code = EditorErrorCode::invalid_argument;
      error.message = "target index is outside the parent's child range";
      error.node_id = reorder.node;
      return error;
    }
    const auto current = std::ranges::find(parent->children, reorder.node);
    if (current == parent->children.end()) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "reordered variation is absent from its parent's children";
      error.node_id = reorder.node;
      return error;
    }
    const auto current_index =
        static_cast<std::size_t>(std::distance(parent->children.begin(), current));
    if (current_index == reorder.target_index) {
      ChangeSet changes;
      changes.before_revision = state_.revision;
      changes.after_revision = state_.revision;
      return CommandResult{state_.revision, std::move(changes), std::nullopt};
    }

    const auto storage_index = *document_.move_tree.storageIndex(reorder.node);
    const auto original_node = *target;
    format::GameDocument working = document_;
    if (!reorder_child(working, parent_id, reorder.node, reorder.target_index)) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "could not reorder the requested variation";
      error.node_id = reorder.node;
      return error;
    }

    const auto before_revision = state_.revision;
    const auto before_token = current_document_token_;
    const auto after_token = next_document_token_;
    HistoryEntry history;
    history.kind = HistoryKind::reorder_variation;
    history.nodes = {{storage_index, original_node}};
    history.root = reorder.node;
    history.parent = parent_id;
    history.sibling_index = current_index;
    history.before_current = state_.current_node;
    history.after_current = state_.current_node;
    history.before_token = before_token;
    history.after_token = after_token;
    history.reordered_index = reorder.target_index;
    if (auto error = store_history(std::move(history)); error.has_value()) {
      return *std::move(error);
    }
    document_ = std::move(working);
    ++state_.revision;
    current_document_token_ = after_token;
    ++next_document_token_;
    state_.dirty = current_document_token_ != saved_document_token_;
    state_.can_undo = true;
    state_.can_redo = false;

    ChangeSet changes;
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
    changes.reordered_parents = {parent_id};
    return CommandResult{state_.revision, std::move(changes), std::nullopt};
  }

  if (std::holds_alternative<ReplaceMoveCommand>(command)) {
    const auto& replacement = std::get<ReplaceMoveCommand>(command);
    if (replacement.node == 0) {
      EditorError error;
      error.code = EditorErrorCode::invalid_argument;
      error.message = "the root node has no move to replace";
      error.node_id = replacement.node;
      return error;
    }
    const auto* target = document_.move_tree.findNode(replacement.node);
    if (target == nullptr) {
      return node_error(replacement.node);
    }
    if (replacement.move.from_square >= 90 || replacement.move.to_square >= 90 ||
        replacement.move.from_square == replacement.move.to_square) {
      EditorError error;
      error.code = EditorErrorCode::invalid_argument;
      error.message = "move squares must be distinct values in the range 0..89";
      error.node_id = replacement.node;
      return error;
    }
    if (target->move == std::optional{replacement.move}) {
      ChangeSet changes;
      changes.before_revision = state_.revision;
      changes.after_revision = state_.revision;
      return CommandResult{state_.revision, std::move(changes), std::nullopt};
    }

    const auto parent_id = *target->parent;
    const auto* parent = document_.move_tree.findNode(parent_id);
    if (parent == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "replaced move has no valid parent";
      error.node_id = replacement.node;
      return error;
    }
    const auto sibling = std::ranges::find(parent->children, replacement.node);
    if (sibling == parent->children.end()) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "replaced move is absent from its parent's children";
      error.node_id = replacement.node;
      return error;
    }
    for (const auto sibling_id : parent->children) {
      if (sibling_id == replacement.node) {
        continue;
      }
      const auto* sibling_node = document_.move_tree.findNode(sibling_id);
      if (sibling_node != nullptr &&
          sibling_node->move == std::optional{replacement.move}) {
        EditorError error;
        error.code = EditorErrorCode::duplicate_move;
        error.message = "a sibling already has the replacement move";
        error.node_id = sibling_id;
        return error;
      }
    }

    const auto original_move = *target->move;
    const auto storage_index = *document_.move_tree.storageIndex(replacement.node);
    const auto sibling_index =
        static_cast<std::size_t>(std::distance(parent->children.begin(), sibling));
    format::GameDocument working = document_;
    auto* working_target = working.move_tree.findNode(replacement.node);
    if (working_target == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "replacement target disappeared from the working document";
      error.node_id = replacement.node;
      return error;
    }
    working_target->move = replacement.move;
    auto issues = validate_document(working, options_);
    if (format::has_errors(issues)) {
      EditorError error;
      error.code = EditorErrorCode::validation_failed;
      error.message = "replacement move invalidates the document or its descendants";
      error.validation_issues = std::move(issues);
      error.node_id = replacement.node;
      return error;
    }

    const auto before_revision = state_.revision;
    const auto before_token = current_document_token_;
    const auto after_token = next_document_token_;
    const auto original_node = *target;
    HistoryEntry history;
    history.kind = HistoryKind::replace_move;
    history.nodes = {{storage_index, original_node}};
    history.root = replacement.node;
    history.parent = parent_id;
    history.sibling_index = sibling_index;
    history.before_current = state_.current_node;
    history.after_current = state_.current_node;
    history.before_token = before_token;
    history.after_token = after_token;
    history.before_move = original_move;
    history.after_move = replacement.move;
    if (auto error = store_history(std::move(history)); error.has_value()) {
      return *std::move(error);
    }
    document_ = std::move(working);
    invalidate_position_subtree(replacement.node);
    ++state_.revision;
    current_document_token_ = after_token;
    ++next_document_token_;
    state_.dirty = current_document_token_ != saved_document_token_;
    state_.can_undo = true;
    state_.can_redo = false;

    ChangeSet changes;
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
    changes.updated = {replacement.node};
    return CommandResult{state_.revision, std::move(changes), std::nullopt};
  }

  if (std::holds_alternative<DeleteSubtreeCommand>(command)) {
    const auto& deletion = std::get<DeleteSubtreeCommand>(command);
    if (deletion.node == 0) {
      EditorError error;
      error.code = EditorErrorCode::invalid_argument;
      error.message = "the root node cannot be deleted";
      error.node_id = deletion.node;
      return error;
    }
    const auto* target = document_.move_tree.findNode(deletion.node);
    if (target == nullptr) {
      return node_error(deletion.node);
    }
    const auto parent_id = *target->parent;
    const auto* parent = document_.move_tree.findNode(parent_id);
    if (parent == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "deleted subtree has no valid parent";
      error.node_id = deletion.node;
      return error;
    }
    const auto sibling = std::ranges::find(parent->children, deletion.node);
    if (sibling == parent->children.end()) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "deleted subtree is absent from its parent's children";
      error.node_id = deletion.node;
      return error;
    }
    const auto sibling_index =
        static_cast<std::size_t>(std::distance(parent->children.begin(), sibling));

    std::unordered_set<format::NodeId> removed_ids;
    std::vector<format::NodeId> pending{deletion.node};
    while (!pending.empty()) {
      const auto current = pending.back();
      pending.pop_back();
      if (!removed_ids.insert(current).second) {
        continue;
      }
      const auto* removed = document_.move_tree.findNode(current);
      if (removed == nullptr) {
        return node_error(current);
      }
      pending.insert(pending.end(), removed->children.begin(), removed->children.end());
    }

    std::vector<StoredNode> stored_nodes;
    std::vector<format::NodeId> removed_order;
    stored_nodes.reserve(removed_ids.size());
    removed_order.reserve(removed_ids.size());
    for (std::size_t index = 0; index < document_.move_tree.nodes.size(); ++index) {
      const auto& candidate = document_.move_tree.nodes[index];
      if (removed_ids.contains(candidate.id)) {
        stored_nodes.push_back({index, candidate});
        removed_order.push_back(candidate.id);
      }
    }

    format::GameDocument working = document_;
    if (!working.move_tree.removeNode(deletion.node)) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "could not remove the requested subtree";
      error.node_id = deletion.node;
      return error;
    }
    auto issues = validate_document(working, options_);
    if (format::has_errors(issues)) {
      EditorError error;
      error.code = EditorErrorCode::validation_failed;
      error.message = "subtree deletion failed document validation";
      error.validation_issues = std::move(issues);
      error.node_id = deletion.node;
      return error;
    }

    const auto before_revision = state_.revision;
    const auto before_current = state_.current_node;
    const auto after_current = removed_ids.contains(before_current) ? parent_id : before_current;
    const auto before_token = current_document_token_;
    const auto after_token = next_document_token_;
    HistoryEntry history;
    history.kind = HistoryKind::delete_subtree;
    history.nodes = std::move(stored_nodes);
    history.root = deletion.node;
    history.parent = parent_id;
    history.sibling_index = sibling_index;
    history.before_current = before_current;
    history.after_current = after_current;
    history.before_token = before_token;
    history.after_token = after_token;
    if (auto error = store_history(std::move(history)); error.has_value()) {
      return *std::move(error);
    }
    document_ = std::move(working);
    invalidate_position_cache(removed_order);
    state_.current_node = after_current;
    ++state_.revision;
    current_document_token_ = after_token;
    ++next_document_token_;
    state_.dirty = current_document_token_ != saved_document_token_;
    state_.can_undo = true;
    state_.can_redo = false;

    ChangeSet changes;
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
    changes.removed = std::move(removed_order);
    changes.reordered_parents = {parent_id};
    changes.selection_changed = before_current != after_current;
    return CommandResult{state_.revision, std::move(changes), std::nullopt};
  }

  const auto& insert = std::get<InsertMoveCommand>(command);
  const auto* parent = document_.move_tree.findNode(insert.parent);
  if (parent == nullptr) {
    return node_error(insert.parent);
  }
  if (insert.move.from_square >= 90 || insert.move.to_square >= 90 ||
      insert.move.from_square == insert.move.to_square) {
    EditorError error;
    error.code = EditorErrorCode::invalid_argument;
    error.message = "move squares must be distinct values in the range 0..89";
    return error;
  }
  const std::size_t insertion_index =
      insert.sibling_index.value_or(parent->children.size());
  if (insertion_index > parent->children.size()) {
    EditorError error;
    error.code = EditorErrorCode::invalid_argument;
    error.message = "sibling index is outside the parent's child range";
    error.node_id = insert.parent;
    return error;
  }
  for (const auto child_id : parent->children) {
    const auto* child = document_.move_tree.findNode(child_id);
    if (child != nullptr && child->move == std::optional{insert.move}) {
      EditorError error;
      error.code = EditorErrorCode::duplicate_move;
      error.message = "the parent already has an identical child move";
      error.node_id = child_id;
      return error;
    }
  }

  format::GameDocument working = document_;
  format::NodeId created{};
  try {
    created = working.move_tree.addNode(insert.parent, insert.move);
    auto* working_parent = working.move_tree.findNode(insert.parent);
    if (working_parent == nullptr) {
      EditorError error;
      error.code = EditorErrorCode::internal_invariant;
      error.message = "inserted move lost its parent";
      return error;
    }
    auto& children = working_parent->children;
    const auto appended = children.back();
    children.pop_back();
    children.insert(children.begin() + static_cast<std::ptrdiff_t>(insertion_index),
                    appended);
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to insert the move";
    return error;
  } catch (const std::exception& exception) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = exception.what();
    return error;
  }

  auto issues = validate_document(working, options_);
  if (format::has_errors(issues)) {
    EditorError error;
    error.code = EditorErrorCode::validation_failed;
    error.message = "inserted move failed document validation";
    error.validation_issues = std::move(issues);
    error.node_id = created;
    return error;
  }

  const auto before_revision = state_.revision;
  const auto before_current = state_.current_node;
  const auto before_token = current_document_token_;
  const auto after_token = next_document_token_;
  const auto inserted_index = *working.move_tree.storageIndex(created);
  const auto inserted_node = *working.move_tree.findNode(created);
  HistoryEntry history;
  history.kind = HistoryKind::insert;
  history.nodes = {{inserted_index, inserted_node}};
  history.root = created;
  history.parent = insert.parent;
  history.sibling_index = insertion_index;
  history.before_current = before_current;
  history.after_current = created;
  history.before_token = before_token;
  history.after_token = after_token;
  if (auto error = store_history(std::move(history)); error.has_value()) {
    return *std::move(error);
  }
  document_ = std::move(working);
  state_.current_node = created;
  ++state_.revision;
  current_document_token_ = after_token;
  ++next_document_token_;
  state_.dirty = current_document_token_ != saved_document_token_;
  state_.can_undo = true;
  state_.can_redo = false;

  ChangeSet changes;
  changes.before_revision = before_revision;
  changes.after_revision = state_.revision;
  changes.inserted = {created};
  changes.reordered_parents = {insert.parent};
  changes.selection_changed = before_current != created;
  return CommandResult{state_.revision, std::move(changes), created};
}

CommandOutcome EditorSession::undo(
    std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }
  if (undo_history_.empty()) {
    EditorError error;
    error.code = EditorErrorCode::history_empty;
    error.message = "there is no command to undo";
    return error;
  }

  HistoryEntry entry;
  format::GameDocument working;
  try {
    entry = undo_history_.back();
    working = document_;
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to prepare undo";
    return error;
  }
  bool mutation_succeeded = false;
  if (entry.kind == HistoryKind::insert) {
    mutation_succeeded = working.move_tree.removeNode(entry.root);
  } else if (entry.kind == HistoryKind::delete_subtree) {
    mutation_succeeded = restore_subtree(working, entry);
  } else if (entry.kind == HistoryKind::replace_move) {
    auto* target = working.move_tree.findNode(entry.root);
    mutation_succeeded = target != nullptr && entry.before_move.has_value();
    if (mutation_succeeded) {
      target->move = entry.before_move;
    }
  } else if (entry.kind == HistoryKind::reorder_variation) {
    mutation_succeeded = reorder_child(working, entry.parent, entry.root,
                                       entry.sibling_index);
  } else if (entry.kind == HistoryKind::set_annotations) {
    auto* target = working.move_tree.findNode(entry.root);
    mutation_succeeded = target != nullptr && entry.before_annotations.has_value();
    if (mutation_succeeded) {
      target->annotations = *entry.before_annotations;
    }
  } else if (entry.kind == HistoryKind::set_metadata) {
    mutation_succeeded = entry.before_metadata.has_value();
    if (mutation_succeeded) {
      working.metadata = *entry.before_metadata;
    }
  } else {
    mutation_succeeded = entry.before_document.has_value();
    if (mutation_succeeded) {
      working = *entry.before_document;
    }
  }
  if (!mutation_succeeded ||
      format::has_errors(validate_document(working, options_))) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "could not undo the editor command";
    error.node_id = entry.root;
    return error;
  }

  std::vector<HistoryEntry> next_undo;
  std::vector<HistoryEntry> next_redo;
  try {
    next_undo = undo_history_;
    next_undo.pop_back();
    next_redo = redo_history_;
    next_redo.push_back(entry);
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to update undo history";
    return error;
  }

  const auto before_revision = state_.revision;
  document_ = std::move(working);
  if (entry.kind == HistoryKind::compound) {
    position_cache_.clear();
  } else if (entry.kind == HistoryKind::replace_move) {
    invalidate_position_subtree(entry.root);
  } else if (entry.kind == HistoryKind::insert) {
    for (const auto& stored : entry.nodes) {
      position_cache_.erase(stored.node.id);
    }
  }
  state_.current_node = entry.before_current;
  ++state_.revision;
  current_document_token_ = entry.before_token;
  state_.dirty = current_document_token_ != saved_document_token_;
  undo_history_.swap(next_undo);
  redo_history_.swap(next_redo);
  state_.can_undo = !undo_history_.empty();
  state_.can_redo = true;

  ChangeSet changes;
  changes.before_revision = before_revision;
  changes.after_revision = state_.revision;
  if (entry.kind == HistoryKind::compound && entry.forward_changes.has_value()) {
    changes = *entry.forward_changes;
    std::swap(changes.inserted, changes.removed);
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
  } else if (entry.kind == HistoryKind::replace_move) {
    changes.updated = {entry.root};
  } else if (entry.kind == HistoryKind::reorder_variation) {
    changes.reordered_parents = {entry.parent};
  } else if (entry.kind == HistoryKind::set_annotations) {
    changes.updated = {entry.root};
  } else if (entry.kind == HistoryKind::set_metadata) {
    changes.metadata_changed = true;
  } else {
    for (const auto& stored : entry.nodes) {
      (entry.kind == HistoryKind::insert ? changes.removed : changes.inserted)
          .push_back(stored.node.id);
    }
    changes.reordered_parents = {entry.parent};
  }
  changes.selection_changed = entry.before_current != entry.after_current;
  return CommandResult{state_.revision, std::move(changes), std::nullopt};
}

CommandOutcome EditorSession::redo(
    std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }
  if (redo_history_.empty()) {
    EditorError error;
    error.code = EditorErrorCode::history_empty;
    error.message = "there is no command to redo";
    return error;
  }

  HistoryEntry entry;
  format::GameDocument working;
  try {
    entry = redo_history_.back();
    working = document_;
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to prepare redo";
    return error;
  }
  bool mutation_succeeded = false;
  if (entry.kind == HistoryKind::insert) {
    mutation_succeeded = restore_subtree(working, entry);
  } else if (entry.kind == HistoryKind::delete_subtree) {
    mutation_succeeded = working.move_tree.removeNode(entry.root);
  } else if (entry.kind == HistoryKind::replace_move) {
    auto* target = working.move_tree.findNode(entry.root);
    mutation_succeeded = target != nullptr && entry.after_move.has_value();
    if (mutation_succeeded) {
      target->move = entry.after_move;
    }
  } else if (entry.kind == HistoryKind::reorder_variation) {
    mutation_succeeded = entry.reordered_index.has_value() &&
                         reorder_child(working, entry.parent, entry.root,
                                       *entry.reordered_index);
  } else if (entry.kind == HistoryKind::set_annotations) {
    auto* target = working.move_tree.findNode(entry.root);
    mutation_succeeded = target != nullptr && entry.after_annotations.has_value();
    if (mutation_succeeded) {
      target->annotations = *entry.after_annotations;
    }
  } else if (entry.kind == HistoryKind::set_metadata) {
    mutation_succeeded = entry.after_metadata.has_value();
    if (mutation_succeeded) {
      working.metadata = *entry.after_metadata;
    }
  } else {
    mutation_succeeded = entry.after_document.has_value();
    if (mutation_succeeded) {
      working = *entry.after_document;
    }
  }
  if (!mutation_succeeded ||
      format::has_errors(validate_document(working, options_))) {
    EditorError error;
    error.code = EditorErrorCode::internal_invariant;
    error.message = "could not redo the editor command";
    error.node_id = entry.root;
    return error;
  }

  std::vector<HistoryEntry> next_undo;
  std::vector<HistoryEntry> next_redo;
  try {
    next_undo = undo_history_;
    next_undo.push_back(entry);
    next_redo = redo_history_;
    next_redo.pop_back();
  } catch (const std::bad_alloc&) {
    EditorError error;
    error.code = EditorErrorCode::resource_limit;
    error.message = "not enough memory to update redo history";
    return error;
  }

  const auto before_revision = state_.revision;
  document_ = std::move(working);
  if (entry.kind == HistoryKind::compound) {
    position_cache_.clear();
  } else if (entry.kind == HistoryKind::replace_move) {
    invalidate_position_subtree(entry.root);
  } else if (entry.kind == HistoryKind::delete_subtree) {
    for (const auto& stored : entry.nodes) {
      position_cache_.erase(stored.node.id);
    }
  }
  state_.current_node = entry.after_current;
  ++state_.revision;
  current_document_token_ = entry.after_token;
  state_.dirty = current_document_token_ != saved_document_token_;
  undo_history_.swap(next_undo);
  redo_history_.swap(next_redo);
  state_.can_undo = true;
  state_.can_redo = !redo_history_.empty();

  ChangeSet changes;
  changes.before_revision = before_revision;
  changes.after_revision = state_.revision;
  if (entry.kind == HistoryKind::compound && entry.forward_changes.has_value()) {
    changes = *entry.forward_changes;
    changes.before_revision = before_revision;
    changes.after_revision = state_.revision;
  } else if (entry.kind == HistoryKind::replace_move) {
    changes.updated = {entry.root};
  } else if (entry.kind == HistoryKind::reorder_variation) {
    changes.reordered_parents = {entry.parent};
  } else if (entry.kind == HistoryKind::set_annotations) {
    changes.updated = {entry.root};
  } else if (entry.kind == HistoryKind::set_metadata) {
    changes.metadata_changed = true;
  } else {
    for (const auto& stored : entry.nodes) {
      (entry.kind == HistoryKind::insert ? changes.inserted : changes.removed)
          .push_back(stored.node.id);
    }
    changes.reordered_parents = {entry.parent};
  }
  changes.selection_changed = entry.before_current != entry.after_current;
  const auto created = entry.kind == HistoryKind::insert
                           ? std::optional<format::NodeId>{entry.root}
                           : std::nullopt;
  return CommandResult{state_.revision, std::move(changes), created};
}

Status EditorSession::checkout(format::NodeId node,
                               std::optional<std::uint64_t> expected_revision) {
  if (expected_revision.has_value() && *expected_revision != state_.revision) {
    return revision_error(*expected_revision, state_.revision,
                          "session revision does not match the expected revision");
  }
  if (document_.move_tree.findNode(node) == nullptr) {
    return node_error(node);
  }
  if (node != state_.current_node) {
    state_.current_node = node;
    ++state_.revision;
  }
  return std::nullopt;
}

Status EditorSession::mark_saved(std::uint64_t revision) {
  if (revision != state_.revision) {
    return revision_error(revision, state_.revision,
                          "saved revision is no longer current");
  }
  saved_document_token_ = current_document_token_;
  state_.dirty = false;
  return std::nullopt;
}

OpenOutcome open_document(format::GameDocument document, SessionOptions options) {
  auto issues = validate_document(document, options);
  if (format::has_errors(issues)) {
    EditorError error;
    error.code = EditorErrorCode::invalid_document;
    error.message = "document failed editor session validation";
    error.validation_issues = std::move(issues);
    return error;
  }
  EditorSession session{std::move(document)};
  session.options_ = options;
  return session;
}

}  // namespace oxq::editor
