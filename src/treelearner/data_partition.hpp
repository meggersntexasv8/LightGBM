#ifndef LIGHTGBM_TREELEARNER_DATA_PARTITION_HPP_
#define LIGHTGBM_TREELEARNER_DATA_PARTITION_HPP_

#include <LightGBM/meta.h>
#include <LightGBM/feature.h>

#include <omp.h>

#include <cstring>

#include <vector>

namespace LightGBM {
/*!
* \brief DataPartition is used to store the the partition of data on tree.
*/
class DataPartition {
public:
  DataPartition(data_size_t num_data, int num_leafs)
    :num_data_(num_data), num_leaves_(num_leafs) {
    leaf_begin_.resize(num_leaves_);
    leaf_count_.resize(num_leaves_);
    indices_.resize(num_data_);
    temp_left_indices_.resize(num_data_);
    temp_right_indices_.resize(num_data_);
    used_data_indices_ = nullptr;
#pragma omp parallel
#pragma omp master
    {
      num_threads_ = omp_get_num_threads();
    }
    offsets_buf_.resize(num_threads_);
    left_cnts_buf_.resize(num_threads_);
    right_cnts_buf_.resize(num_threads_);
    left_write_pos_buf_.resize(num_threads_);
    right_write_pos_buf_.resize(num_threads_);
  }
  ~DataPartition() {

  }

  /*!
  * \brief Init, will put all data on the root(leaf_idx = 0)
  */
  void Init() {
    for (int i = 0; i < num_leaves_; ++i) {
      leaf_count_[i] = 0;
    }
    leaf_begin_[0] = 0;
    if (used_data_indices_ == nullptr) {
      // if using all data
      leaf_count_[0] = num_data_;
#pragma omp parallel for schedule(static)
      for (data_size_t i = 0; i < num_data_; ++i) {
        indices_[i] = i;
      }
    } else {
      // if bagging
      leaf_count_[0] = used_data_count_;
      std::memcpy(indices_.data(), used_data_indices_, used_data_count_ * sizeof(data_size_t));
    }
  }

  /*!
  * \brief Get the data indices of one leaf
  * \param leaf index of leaf
  * \param indices output data indices
  * \return number of data on this leaf
  */
  const data_size_t* GetIndexOnLeaf(int leaf, data_size_t* out_len) const {
    // copy reference, maybe unsafe, but faster
    data_size_t begin = leaf_begin_[leaf];
    *out_len = leaf_count_[leaf];
    return indices_.data() + begin;
  }

  /*!
  * \brief Split the data
  * \param leaf index of leaf
  * \param feature_bins feature bin data
  * \param threshold threshold that want to split
  * \param right_leaf index of right leaf
  */
  void Split(int leaf, const Bin* feature_bins, unsigned int threshold, int right_leaf) {
    const data_size_t min_inner_size = 1000;
    // get leaf boundary
    const data_size_t begin = leaf_begin_[leaf];
    const data_size_t cnt = leaf_count_[leaf];

    data_size_t inner_size = (cnt + num_threads_ - 1) / num_threads_;
    if (inner_size < min_inner_size) { inner_size = min_inner_size; }
    // split data multi-threading
#pragma omp parallel for schedule(static, 1)
    for (int i = 0; i < num_threads_; ++i) {
      left_cnts_buf_[i] = 0;
      right_cnts_buf_[i] = 0;
      data_size_t cur_start = i * inner_size;
      if (cur_start > cnt) { continue; }
      data_size_t cur_cnt = inner_size;
      if (cur_start + cur_cnt > cnt) { cur_cnt = cnt - cur_start; }
      // split data inner, reduce the times of function called
      data_size_t cur_left_count = feature_bins->Split(threshold, indices_.data() + begin + cur_start, cur_cnt,
        temp_left_indices_.data() + cur_start, temp_right_indices_.data() + cur_start);
      offsets_buf_[i] = cur_start;
      left_cnts_buf_[i] = cur_left_count;
      right_cnts_buf_[i] = cur_cnt - cur_left_count;
    }
    data_size_t left_cnt = 0;
    left_write_pos_buf_[0] = 0;
    right_write_pos_buf_[0] = 0;
    for (int i = 1; i < num_threads_; ++i) {
      left_write_pos_buf_[i] = left_write_pos_buf_[i - 1] + left_cnts_buf_[i - 1];
      right_write_pos_buf_[i] = right_write_pos_buf_[i - 1] + right_cnts_buf_[i - 1];
    }
    left_cnt = left_write_pos_buf_[num_threads_ - 1] + left_cnts_buf_[num_threads_ - 1];
    // copy back indices of right leaf to indices_
#pragma omp parallel for schedule(static, 1)
    for (int i = 0; i < num_threads_; ++i) {
      if (left_cnts_buf_[i] > 0) {
        std::memcpy(indices_.data() + begin + left_write_pos_buf_[i], 
          temp_left_indices_.data() + offsets_buf_[i], left_cnts_buf_[i] * sizeof(data_size_t));
      }
      if (right_cnts_buf_[i] > 0) {
        std::memcpy(indices_.data() + begin + left_cnt + right_write_pos_buf_[i], 
          temp_right_indices_.data() + offsets_buf_[i], right_cnts_buf_[i] * sizeof(data_size_t));
      }
    }
    // update leaf boundary
    leaf_count_[leaf] = left_cnt;
    leaf_begin_[right_leaf] = left_cnt + begin;
    leaf_count_[right_leaf] = cnt - left_cnt;
  }

  /*!
  * \brief SetLabelAt used data indices before training, used for bagging
  * \param used_data_indices indices of used data
  * \param num_used_data number of used data
  */
  void SetUsedDataIndices(const data_size_t* used_data_indices, data_size_t num_used_data) {
    used_data_indices_ = used_data_indices;
    used_data_count_ = num_used_data;
  }

  /*!
  * \brief Get number of data on one leaf
  * \param leaf index of leaf
  * \return number of data of this leaf
  */
  data_size_t leaf_count(int leaf) const { return leaf_count_[leaf]; }

  /*!
  * \brief Get leaf begin
  * \param leaf index of leaf
  * \return begin index of this leaf
  */
  data_size_t leaf_begin(int leaf) const { return leaf_begin_[leaf]; }

  const data_size_t* indices() const { return indices_.data(); }

  /*! \brief Get number of leaves */
  int num_leaves() const { return num_leaves_; }

private:
  /*! \brief Number of all data */
  data_size_t num_data_;
  /*! \brief Number of all leaves */
  int num_leaves_;
  /*! \brief start index of data on one leaf */
  std::vector<data_size_t> leaf_begin_;
  /*! \brief number of data on one leaf */
  std::vector<data_size_t> leaf_count_;
  /*! \brief Store all data's indices, order by leaf[data_in_leaf0,..,data_leaf1,..] */
  std::vector<data_size_t> indices_;
  /*! \brief team indices buffer for split */
  std::vector<data_size_t> temp_left_indices_;
  /*! \brief team indices buffer for split */
  std::vector<data_size_t> temp_right_indices_;
  /*! \brief used data indices, used for bagging */
  const data_size_t* used_data_indices_;
  /*! \brief used data count, used for bagging */
  data_size_t used_data_count_;
  /*! \brief number of threads */
  int num_threads_;
  /*! \brief Buffer for multi-threading data partition, used to store offset for different threads */
  std::vector<data_size_t> offsets_buf_;
  /*! \brief Buffer for multi-threading data partition, used to store left count after split for different threads */
  std::vector<data_size_t> left_cnts_buf_;
  /*! \brief Buffer for multi-threading data partition, used to store right count after split for different threads */
  std::vector<data_size_t> right_cnts_buf_;
  /*! \brief Buffer for multi-threading data partition, used to store write position of left leaf for different threads */
  std::vector<data_size_t> left_write_pos_buf_;
  /*! \brief Buffer for multi-threading data partition, used to store write position of right leaf for different threads */
  std::vector<data_size_t> right_write_pos_buf_;
};

}  // namespace LightGBM
#endif   // LightGBM_TREELEARNER_DATA_PARTITION_HPP_
