#pragma once

#include <core/BasicTypes.hpp>
#include <core/Constants.hpp>
#include <core/concepts/Game.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace core {

struct ShapeInfo {
  template <eigen_util::concepts::FTensor Tensor> void init(const char* name, int target_index);
  ~ShapeInfo();

  const char* name = nullptr;
  int* dims = nullptr;
  int num_dims = 0;
  int target_index = -1;
};

struct GameLogBase {
  static constexpr int kAlignment = 16;

  struct Header {
    uint32_t num_samples = 0;
    uint32_t num_positions = 0;  // includes terminal positions
    uint32_t num_dense_policies = 0;
    uint32_t num_sparse_policy_entries = 0;
    uint32_t extra = 0;  // leave extra space for future use (version numbering, etc.)
  };

  using pos_index_t = int32_t;

#pragma pack(push, 1)
  struct policy_target_index_t {
    // If start < end, then the representation is sparse, and entries [start, end) of
    // the sparse_policy_entry_t region represent the sparse tensor.
    // Else, if start == end >= 0, then start is the index into the dense tensor region.
    // Else, if start == end < 0, then there is no policy target.
    // Any other start/end pair is invalid.
    int16_t start;
    int16_t end;
  };
#pragma pack(pop)

  struct sparse_policy_entry_t {
    int32_t offset;
    float probability;
  };

  static constexpr int align(int offset);
};

/*
 * GameLog file format is as follows:
 *
 * [Header]
 * [ValueArray]
 * [pos_index_t...]                // indices of sampled positions
 * [action_t...]
 * [policy_target_index_t...]
 * [Game::BaseState...]            // all positions, whether sampled or not
 * [Game::Types::PolicyTensor...]  // data for densely represented policy targets
 * [sparse_policy_entry_t...]      // data for sparsely represented policy targets
 *
 * Each section is aligned to 8 bytes.
 */
template <concepts::Game Game>
class GameLog : public GameLogBase {
 public:
  using Header = GameLogBase::Header;

  using Rules = Game::Rules;
  using InputTensorizor = Game::InputTensorizor;
  using InputTensor = Game::InputTensorizor::Tensor;
  using TrainingTargetsList = Game::TrainingTargets::List;
  using BaseState = Game::BaseState;
  using PolicyTensor = Game::Types::PolicyTensor;
  using ValueArray = Game::Types::ValueArray;
  using GameLogView = Game::Types::GameLogView;

  GameLog(const char* filename);
  ~GameLog();

  static ShapeInfo* get_shape_info_array();

  void load(int index, bool apply_symmetry, float* input_values, int* target_indices,
            float** target_value_arrays) const;

  void replay() const;
  int num_sampled_positions() const;

 private:
  char* get_buffer() const;
  Header& header();
  const Header& header() const;

  int num_positions() const;
  int num_non_terminal_positions() const;
  int num_dense_policies() const;
  int num_sparse_policy_entries() const;

  static constexpr int header_start_mem_offset();
  static constexpr int outcome_start_mem_offset();
  static constexpr int sampled_indices_start_mem_offset();
  int action_start_mem_offset() const;
  int policy_target_index_start_mem_offset() const;
  int state_start_mem_offset() const;
  int dense_policy_start_mem_offset() const;
  int sparse_policy_entry_start_mem_offset() const;

  const action_t* action_start_ptr() const;
  const policy_target_index_t* policy_target_index_start_ptr() const;
  const BaseState* state_start_ptr() const;
  const PolicyTensor* dense_policy_start_ptr() const;
  const sparse_policy_entry_t* sparse_policy_entry_start_ptr() const;
  const pos_index_t* sampled_indices_start_ptr() const;

  PolicyTensor get_policy(int state_index) const;
  const BaseState* get_state(int state_index) const;
  action_t get_prev_action(int state_index) const;
  ValueArray get_outcome() const;
  pos_index_t get_pos_index(int sample_index) const;

  const std::string filename_;
  char* const buffer_ = nullptr;
  const int action_start_mem_offset_;
  const int policy_target_index_start_mem_offset_;
  const int state_start_mem_offset_;
  const int dense_policy_start_mem_offset_;
  const int sparse_policy_entry_start_mem_offset_;
};

template <concepts::Game Game>
class GameLogWriter {
 public:
  using Rules = Game::Rules;
  using BaseState = Game::BaseState;
  using FullState = Game::FullState;
  using ValueArray = Game::Types::ValueArray;
  using PolicyTensor = Game::Types::PolicyTensor;
  using sparse_policy_entry_t = GameLogBase::sparse_policy_entry_t;

  struct Entry {
    BaseState position;
    PolicyTensor policy_target;  // only valid if policy_target_is_valid
    action_t action;
    bool use_for_training;
    bool policy_target_is_valid;
    bool terminal;
  };
  using entry_vector_t = std::vector<Entry*>;

  GameLogWriter(game_id_t id, int64_t start_timestamp);
  ~GameLogWriter();

  void add(const FullState& state, action_t action, const PolicyTensor* policy_target,
           bool use_for_training);
  void add_terminal(const FullState& state, const ValueArray& outcome);
  void serialize(std::ostream&) const;
  bool is_previous_entry_used_for_training() const;
  int sample_count() const { return sample_count_; }
  game_id_t id() const { return id_; }
  int64_t start_timestamp() const { return start_timestamp_; }
  void close() { closed_ = true; }
  bool closed() const { return closed_; }

 private:
  template<typename T>
  static void write_section(std::ostream& os, const T* t, int count=1);

  static GameLogBase::policy_target_index_t write_policy_target(
      const Entry& entry, std::vector<PolicyTensor>& dense_tensors,
      std::vector<GameLogBase::sparse_policy_entry_t>& sparse_tensor_entries);

  entry_vector_t entries_;
  ValueArray outcome_;
  const game_id_t id_;
  const int64_t start_timestamp_;
  int sample_count_ = 0;
  bool terminal_added_ = false;
  bool closed_ = false;
};

}  // namespace core

#include <inline/core/GameLog.inl>
