#include <omp.h>

#include <LightGBM/utils/common.h>
#include <LightGBM/utils/random.h>
#include <LightGBM/c_api.h>
#include <LightGBM/dataset_loader.h>
#include <LightGBM/dataset.h>
#include <LightGBM/boosting.h>
#include <LightGBM/objective_function.h>
#include <LightGBM/metric.h>
#include <LightGBM/config.h>

#include <cstdio>
#include <vector>
#include <string>
#include <cstring>
#include <memory>

#include "./application/predictor.hpp"

namespace LightGBM {

class Booster {
public:
  explicit Booster(const char* filename):
    boosting_(Boosting::CreateBoosting(filename)), predictor_(nullptr) {
  }

  Booster(const Dataset* train_data, 
    std::vector<const Dataset*> valid_data, 
    std::vector<std::string> valid_names,
    const char* parameters)
    :train_data_(train_data), valid_datas_(valid_data), predictor_(nullptr) {
    config_.LoadFromString(parameters);
    // create boosting
    if (config_.io_config.input_model.size() > 0) {
      Log::Warning("continued train from model is not support for c_api, \
        please use continued train with input score");
    }
    boosting_ = Boosting::CreateBoosting(config_.boosting_type, "");
    // create objective function
    objective_fun_ =
      ObjectiveFunction::CreateObjectiveFunction(config_.objective_type,
        config_.objective_config);
    // create training metric
    for (auto metric_type : config_.metric_types) {
      Metric* metric =
        Metric::CreateMetric(metric_type, config_.metric_config);
      if (metric == nullptr) { continue; }
      metric->Init("training", train_data_->metadata(),
        train_data_->num_data());
      train_metric_.push_back(metric);
    }
    
    // add metric for validation data
    for (size_t i = 0; i < valid_datas_.size(); ++i) {
      valid_metrics_.emplace_back();
      for (auto metric_type : config_.metric_types) {
        Metric* metric = Metric::CreateMetric(metric_type, config_.metric_config);
        if (metric == nullptr) { continue; }
        metric->Init(valid_names[i].c_str(),
          valid_datas_[i]->metadata(),
          valid_datas_[i]->num_data());
        valid_metrics_.back().push_back(metric);
      }
    }
    // initialize the objective function
    objective_fun_->Init(train_data_->metadata(), train_data_->num_data());
    // initialize the boosting
    boosting_->Init(config_.boosting_config, train_data_, objective_fun_,
      ConstPtrInVectorWarpper<Metric>(train_metric_));
    // add validation data into boosting
    for (size_t i = 0; i < valid_datas_.size(); ++i) {
      boosting_->AddDataset(valid_datas_[i],
        ConstPtrInVectorWarpper<Metric>(valid_metrics_[i]));
    }
  }

  ~Booster() {
    for (auto& metric : train_metric_) {
      if (metric != nullptr) { delete metric; }
    }
    for (auto& metric : valid_metrics_) {
      for (auto& sub_metric : metric) {
        if (sub_metric != nullptr) { delete sub_metric; }
      }
    }
    valid_metrics_.clear();
    if (boosting_ != nullptr) { delete boosting_; }
    if (objective_fun_ != nullptr) { delete objective_fun_; }
    if (predictor_ != nullptr) { delete predictor_; }
  }

  bool TrainOneIter() {
    return boosting_->TrainOneIter(nullptr, nullptr, false);
  }

  bool TrainOneIter(const float* gradients, const float* hessians) {
    return boosting_->TrainOneIter(gradients, hessians, false);
  }

  void PrepareForPrediction(int num_used_model, int predict_type) {
    boosting_->SetNumUsedModel(num_used_model);
    if (predictor_ != nullptr) { delete predictor_; }
    bool is_predict_leaf = false;
    bool is_raw_score = false;
    if (predict_type == 2) {
      is_predict_leaf = true;
    } else if (predict_type == 1) {
      is_raw_score = false;
    } else {
      is_raw_score = true;
    }
    predictor_ = new Predictor(boosting_, is_raw_score, is_predict_leaf);
  }

  std::vector<double> Predict(const std::vector<std::pair<int, double>>& features) {
    return predictor_->GetPredictFunction()(features);
  }

  void SaveModelToFile(int num_used_model, const char* filename) {
    boosting_->SaveModelToFile(num_used_model, true, filename);
  }
  const Boosting* GetBoosting() const { return boosting_; }

  const inline int NumberOfClasses() const { return boosting_->NumberOfClasses(); }

private:

  Boosting* boosting_;
  /*! \brief All configs */
  OverallConfig config_;
  /*! \brief Training data */
  const Dataset* train_data_;
  /*! \brief Validation data */
  std::vector<const Dataset*> valid_datas_;
  /*! \brief Metric for training data */
  std::vector<Metric*> train_metric_;
  /*! \brief Metrics for validation data */
  std::vector<std::vector<Metric*>> valid_metrics_;
  /*! \brief Training objective function */
  ObjectiveFunction* objective_fun_;
  /*! \brief Using predictor for prediction task */
  Predictor* predictor_;

};

}

using namespace LightGBM;


DllExport const char* LGBM_GetLastError() {
  return "Not error msg now, will support soon";
}



DllExport int LGBM_CreateDatasetFromFile(const char* filename,
  const char* parameters,
  const DatesetHandle* reference,
  DatesetHandle* out) {

  OverallConfig config;
  config.LoadFromString(parameters);
  DatasetLoader loader(config.io_config, nullptr);
  if (reference == nullptr) {
    *out = loader.LoadFromFile(filename);
  } else {
    *out = loader.LoadFromFileAlignWithOtherDataset(filename, reinterpret_cast<const Dataset*>(*reference));
  }
  return 0;
}


DllExport int LGBM_CreateDatasetFromBinaryFile(const char* filename,
  DatesetHandle* out) {

  OverallConfig config;
  DatasetLoader loader(config.io_config, nullptr);
  *out = loader.LoadFromBinFile(filename, 0, 1);
  return 0;
}

DllExport int LGBM_CreateDatasetFromMat(const void* data,
  int data_type,
  int32_t nrow,
  int32_t ncol,
  int is_row_major,
  const char* parameters,
  const DatesetHandle* reference,
  DatesetHandle* out) {

  OverallConfig config;
  config.LoadFromString(parameters);
  DatasetLoader loader(config.io_config, nullptr);
  Dataset* ret = nullptr;
  auto get_row_fun = RowFunctionFromDenseMatric(data, nrow, ncol, data_type, is_row_major);
  if (reference == nullptr) {
    // sample data first
    Random rand(config.io_config.data_random_seed);
    const size_t sample_cnt = static_cast<size_t>(nrow < config.io_config.bin_construct_sample_cnt ? nrow : config.io_config.bin_construct_sample_cnt);
    auto sample_indices = rand.Sample(nrow, sample_cnt);
    std::vector<std::vector<double>> sample_values(ncol);
    for (size_t i = 0; i < sample_indices.size(); ++i) {
      auto idx = sample_indices[i];
      auto row = get_row_fun(static_cast<int>(idx));
      for (size_t j = 0; j < row.size(); ++j) {
        sample_values[j].push_back(row[j]);
      }
    }
    ret = loader.CostructFromSampleData(sample_values, nrow);
  } else {
    ret = new Dataset(nrow, config.io_config.num_class);
    reinterpret_cast<const Dataset*>(*reference)->CopyFeatureBinMapperTo(ret, config.io_config.is_enable_sparse);
  }

#pragma omp parallel for schedule(guided)
  for (int i = 0; i < nrow; ++i) {
    const int tid = omp_get_thread_num();
    auto one_row = get_row_fun(i);
    ret->PushOneRow(tid, i, one_row);
  }
  ret->FinishLoad();
  *out = ret;
  return 0;
}

DllExport int LGBM_CreateDatasetFromCSR(const void* indptr,
  int indptr_type,
  const int32_t* indices,
  const void* data,
  int data_type,
  int64_t nindptr,
  int64_t nelem,
  int64_t num_col,
  const char* parameters,
  const DatesetHandle* reference,
  DatesetHandle* out) {

  OverallConfig config;
  config.LoadFromString(parameters);
  DatasetLoader loader(config.io_config, nullptr);
  Dataset* ret = nullptr;
  auto get_row_fun = RowFunctionFromCSR(indptr, indptr_type, indices, data, data_type, nindptr, nelem);
  int32_t nrow = static_cast<int32_t>(nindptr - 1);
  if (reference == nullptr) {
    // sample data first
    Random rand(config.io_config.data_random_seed);
    const size_t sample_cnt = static_cast<size_t>(nrow < config.io_config.bin_construct_sample_cnt ? nrow : config.io_config.bin_construct_sample_cnt);
    auto sample_indices = rand.Sample(nrow, sample_cnt);
    std::vector<std::vector<double>> sample_values;
    for (size_t i = 0; i < sample_indices.size(); ++i) {
      auto idx = sample_indices[i];
      auto row = get_row_fun(static_cast<int>(idx));
      // push 0 first, then edit the value according existing feature values
      for (auto& feature_values : sample_values) {
        feature_values.push_back(0.0);
      }
      for (std::pair<int, double>& inner_data : row) {
        if (static_cast<size_t>(inner_data.first) >= sample_values.size()) {
          // if need expand feature set
          size_t need_size = inner_data.first - sample_values.size() + 1;
          for (size_t j = 0; j < need_size; ++j) {
            // push i+1 0
            sample_values.emplace_back(i + 1, 0.0f);
          }
        }
        // edit the feature value
        sample_values[inner_data.first][i] = inner_data.second;
      }
    }
    CHECK(num_col >= static_cast<int>(sample_values.size()));
    ret = loader.CostructFromSampleData(sample_values, nrow);
  } else {
    ret = new Dataset(nrow, config.io_config.num_class);
    reinterpret_cast<const Dataset*>(*reference)->CopyFeatureBinMapperTo(ret, config.io_config.is_enable_sparse);
  }

#pragma omp parallel for schedule(guided)
  for (int i = 0; i < nindptr - 1; ++i) {
    const int tid = omp_get_thread_num();
    auto one_row = get_row_fun(i);
    ret->PushOneRow(tid, i, one_row);
  }
  ret->FinishLoad();
  *out = ret;

  return 0;
}


DllExport int LGBM_CreateDatasetFromCSC(const void* col_ptr,
  int col_ptr_type,
  const int32_t* indices,
  const void* data,
  int data_type,
  int64_t ncol_ptr,
  int64_t nelem,
  int64_t num_row,
  const char* parameters,
  const DatesetHandle* reference,
  DatesetHandle* out) {
  OverallConfig config;
  config.LoadFromString(parameters);
  DatasetLoader loader(config.io_config, nullptr);
  Dataset* ret = nullptr;
  auto get_col_fun = ColumnFunctionFromCSC(col_ptr, col_ptr_type, indices, data, data_type, ncol_ptr, nelem);
  int32_t nrow = static_cast<int32_t>(num_row);
  if (reference == nullptr) {
    Log::Warning("Construct from CSC format is not efficient");
    // sample data first
    Random rand(config.io_config.data_random_seed);
    const size_t sample_cnt = static_cast<size_t>(nrow < config.io_config.bin_construct_sample_cnt ? nrow : config.io_config.bin_construct_sample_cnt);
    auto sample_indices = rand.Sample(nrow, sample_cnt);
    std::vector<std::vector<double>> sample_values(ncol_ptr - 1);
#pragma omp parallel for schedule(guided)
    for (int i = 0; i < static_cast<int>(sample_values.size()); ++i) {
      auto cur_col = get_col_fun(i);
      sample_values[i] = SampleFromOneColumn(cur_col, sample_indices);
    }
    ret = loader.CostructFromSampleData(sample_values, nrow);
  } else {
    ret = new Dataset(nrow, config.io_config.num_class);
    reinterpret_cast<const Dataset*>(*reference)->CopyFeatureBinMapperTo(ret, config.io_config.is_enable_sparse);
  }

#pragma omp parallel for schedule(guided)
  for (int i = 0; i < ncol_ptr - 1; ++i) {
    const int tid = omp_get_thread_num();
    auto one_col = get_col_fun(i);
    ret->PushOneColumn(tid, i, one_col);
  }
  ret->FinishLoad();
  *out = ret;
  return 0;
}

DllExport int LGBM_DatasetFree(DatesetHandle* handle) {
  auto dataset = reinterpret_cast<Dataset*>(*handle);
  delete dataset;
  return 0;
}

DllExport int LGBM_DatasetSaveBinary(DatesetHandle handle,
  const char* filename) {
  auto dataset = reinterpret_cast<Dataset*>(handle);
  dataset->SaveBinaryFile(filename);
  return 0;
}

DllExport int LGBM_DatasetSetField(DatesetHandle handle,
  const char* field_name,
  const void* field_data,
  int64_t num_element,
  int type) {
  auto dataset = reinterpret_cast<Dataset*>(handle);
  bool is_success = false;
  if (type == dtype_float32) {
    is_success = dataset->SetFloatField(field_name, reinterpret_cast<const float*>(field_data), static_cast<int32_t>(num_element));
  } else if (type == dtype_int32) {
    is_success = dataset->SetIntField(field_name, reinterpret_cast<const int*>(field_data), static_cast<int32_t>(num_element));
  }
  if (is_success) { return 0; }
  return -1;
}

DllExport int LGBM_DatasetGetField(DatesetHandle handle,
  const char* field_name,
  int64_t* out_len,
  const void** out_ptr,
  int* out_type) {
  auto dataset = reinterpret_cast<Dataset*>(handle);
  if (dataset->GetFloatField(field_name, out_len, reinterpret_cast<const float**>(out_ptr))) {
    *out_type = dtype_float32;
    return 0;
  } else if (dataset->GetIntField(field_name, out_len, reinterpret_cast<const int**>(out_ptr))) {
    *out_type = dtype_int32;
    return 0;
  }
  return -1;
}

DllExport int LGBM_DatasetGetNumData(DatesetHandle handle,
  int64_t* out) {
  auto dataset = reinterpret_cast<Dataset*>(handle);
  *out = dataset->num_data();
  return 0;
}

DllExport int LGBM_DatasetGetNumFeature(DatesetHandle handle,
  int64_t* out) {
  auto dataset = reinterpret_cast<Dataset*>(handle);
  *out = dataset->num_total_features();
  return 0;
}


// ---- start of booster

DllExport int LGBM_BoosterCreate(const DatesetHandle train_data,
  const DatesetHandle valid_datas[],
  const char* valid_names[],
  int n_valid_datas,
  const char* parameters,
  BoosterHandle* out) {
  const Dataset* p_train_data = reinterpret_cast<const Dataset*>(train_data);
  std::vector<const Dataset*> p_valid_datas;
  std::vector<std::string> p_valid_names;
  for (int i = 0; i < n_valid_datas; ++i) {
    p_valid_datas.emplace_back(reinterpret_cast<const Dataset*>(valid_datas[i]));
    p_valid_names.emplace_back(valid_names[i]);
  }
  *out = new Booster(p_train_data, p_valid_datas, p_valid_names, parameters);
  return 0;
}

DllExport int LGBM_BoosterLoadFromModelfile(
  const char* filename,
  BoosterHandle* out) {
  *out = new Booster(filename);
  return 0;
}

DllExport int LGBM_BoosterFree(BoosterHandle handle) {
  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  delete ref_booster;
  return 0;
}


DllExport int LGBM_BoosterUpdateOneIter(BoosterHandle handle, int* is_finished) {
  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  if (ref_booster->TrainOneIter()) {
    *is_finished = 1;
  } else {
    *is_finished = 0;
  }
  return 0;
}

DllExport int LGBM_BoosterUpdateOneIterCustom(BoosterHandle handle,
  const float* grad,
  const float* hess,
  int* is_finished) {
  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  if (ref_booster->TrainOneIter(grad, hess)) {
    *is_finished = 1;
  } else {
    *is_finished = 0;
  }
  return 0;
}

DllExport int LGBM_BoosterEval(BoosterHandle handle,
  int data,
  int64_t* out_len,
  float* out_results) {

  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  auto boosting = ref_booster->GetBoosting();
  auto result_buf = boosting->GetEvalAt(data);
  *out_len = static_cast<int64_t>(result_buf.size());
  for (size_t i = 0; i < result_buf.size(); ++i) {
    (out_results)[i] = static_cast<float>(result_buf[i]);
  }
  return 0;
}

DllExport int LGBM_BoosterGetScore(BoosterHandle handle,
  int64_t* out_len,
  const float** out_result) {

  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  auto boosting = ref_booster->GetBoosting();
  int len = 0;
  *out_result = boosting->GetTrainingScore(&len);
  *out_len = static_cast<int64_t>(len);

  return 0;
}

DllExport int LGBM_BoosterGetPredict(BoosterHandle handle,
  int data,
  int64_t* out_len,
  float* out_result) {

  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  auto boosting = ref_booster->GetBoosting();
  int len = 0;
  boosting->GetPredictAt(data, out_result, &len);
  *out_len = static_cast<int64_t>(len);
  return 0;
}

DllExport int LGBM_BoosterPredictForCSR(BoosterHandle handle,
  const void* indptr,
  int indptr_type,
  const int32_t* indices,
  const void* data,
  int data_type,
  int64_t nindptr,
  int64_t nelem,
  int64_t,
  int predict_type,
  int64_t n_used_trees,
  double* out_result) {

  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  ref_booster->PrepareForPrediction(static_cast<int>(n_used_trees), predict_type);

  auto get_row_fun = RowFunctionFromCSR(indptr, indptr_type, indices, data, data_type, nindptr, nelem);
  int num_class = ref_booster->NumberOfClasses();
  int nrow = static_cast<int>(nindptr - 1);
#pragma omp parallel for schedule(guided)
  for (int i = 0; i < nrow; ++i) {
    auto one_row = get_row_fun(i);
    auto predicton_result = ref_booster->Predict(one_row);
    for (int j = 0; j < num_class; ++j) {
      out_result[i * num_class + j] = predicton_result[j];
    }
  }
  return 0;
}

DllExport int LGBM_BoosterPredictForMat(BoosterHandle handle,
  const void* data,
  int data_type,
  int32_t nrow,
  int32_t ncol,
  int is_row_major,
  int predict_type,
  int64_t n_used_trees,
  double* out_result) {

  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  ref_booster->PrepareForPrediction(static_cast<int>(n_used_trees), predict_type);

  auto get_row_fun = RowPairFunctionFromDenseMatric(data, nrow, ncol, data_type, is_row_major);
  int num_class = ref_booster->NumberOfClasses();
#pragma omp parallel for schedule(guided)
  for (int i = 0; i < nrow; ++i) {
    auto one_row = get_row_fun(i);
    auto predicton_result = ref_booster->Predict(one_row);
    for (int j = 0; j < num_class; ++j) {
      out_result[i * num_class + j] = predicton_result[j];
    }
  }
  return 0;
}

DllExport int LGBM_BoosterSaveModel(BoosterHandle handle,
  int num_used_model,
  const char* filename) {

  Booster* ref_booster = reinterpret_cast<Booster*>(handle);
  ref_booster->SaveModelToFile(num_used_model, filename);
  return 0;
}

// ---- start of some help functions

std::function<std::vector<double>(int row_idx)>
RowFunctionFromDenseMatric(const void* data, int num_row, int num_col, int data_type, int is_row_major) {
  if (data_type == dtype_float32) {
    const float* data_ptr = reinterpret_cast<const float*>(data);
    if (is_row_major) {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<double> ret;
        auto tmp_ptr = data_ptr + num_col * row_idx;
        for (int i = 0; i < num_col; ++i) {
          ret.push_back(static_cast<double>(*(tmp_ptr + i)));
        }
        return ret;
      };
    } else {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<double> ret;
        for (int i = 0; i < num_col; ++i) {
          ret.push_back(static_cast<double>(*(data_ptr + num_row * i + row_idx)));
        }
        return ret;
      };
    }
  } else if (data_type == dtype_float64) {
    const double* data_ptr = reinterpret_cast<const double*>(data);
    if (is_row_major) {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<double> ret;
        auto tmp_ptr = data_ptr + num_col * row_idx;
        for (int i = 0; i < num_col; ++i) {
          ret.push_back(static_cast<double>(*(tmp_ptr + i)));
        }
        return ret;
      };
    } else {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<double> ret;
        for (int i = 0; i < num_col; ++i) {
          ret.push_back(static_cast<double>(*(data_ptr + num_row * i + row_idx)));
        }
        return ret;
      };
    }
  } else {
    Log::Fatal("unknown data type in RowFunctionFromDenseMatric");
  }
  return nullptr;
}

std::function<std::vector<std::pair<int, double>>(int row_idx)>
RowPairFunctionFromDenseMatric(const void* data, int num_row, int num_col, int data_type, int is_row_major) {
  if (data_type == dtype_float32) {
    const float* data_ptr = reinterpret_cast<const float*>(data);
    if (is_row_major) {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<std::pair<int, double>> ret;
        auto tmp_ptr = data_ptr + num_col * row_idx;
        for (int i = 0; i < num_col; ++i) {
          ret.emplace_back(i, static_cast<double>(*(tmp_ptr + i)));
        }
        return ret;
      };
    } else {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<std::pair<int, double>> ret;
        for (int i = 0; i < num_col; ++i) {
          ret.emplace_back(i, static_cast<double>(*(data_ptr + num_row * i + row_idx)));
        }
        return ret;
      };
    }
  } else if (data_type == dtype_float64) {
    const double* data_ptr = reinterpret_cast<const double*>(data);
    if (is_row_major) {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<std::pair<int, double>> ret;
        auto tmp_ptr = data_ptr + num_col * row_idx;
        for (int i = 0; i < num_col; ++i) {
          ret.emplace_back(i, static_cast<double>(*(tmp_ptr + i)));
        }
        return ret;
      };
    } else {
      return [data_ptr, num_col, num_row](int row_idx) {
        CHECK(row_idx < num_row);
        std::vector<std::pair<int, double>> ret;
        for (int i = 0; i < num_col; ++i) {
          ret.emplace_back(i, static_cast<double>(*(data_ptr + num_row * i + row_idx)));
        }
        return ret;
      };
    }
  } else {
    Log::Fatal("unknown data type in RowPairFunctionFromDenseMatric");
  }
  return nullptr;
}

std::function<std::vector<std::pair<int, double>>(int idx)>
RowFunctionFromCSR(const void* indptr, int indptr_type, const int32_t* indices, const void* data, int data_type, int64_t nindptr, int64_t nelem) {
  if (data_type == dtype_float32) {
    const float* data_ptr = reinterpret_cast<const float*>(data);
    if (indptr_type == dtype_int32) {
      const int32_t* ptr_indptr = reinterpret_cast<const int32_t*>(indptr);
      return [ptr_indptr, indices, data_ptr, nindptr, nelem](int idx) {
        CHECK(idx + 1 < nindptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_indptr[idx];
        int64_t end = ptr_indptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i <= end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else if (indptr_type == dtype_int64) {
      const int64_t* ptr_indptr = reinterpret_cast<const int64_t*>(indptr);
      return [ptr_indptr, indices, data_ptr, nindptr, nelem](int idx) {
        CHECK(idx + 1 < nindptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_indptr[idx];
        int64_t end = ptr_indptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i <= end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else {
      Log::Fatal("unknown data type in RowFunctionFromCSR");
    }
  } else if (data_type == dtype_float64) {
    const double* data_ptr = reinterpret_cast<const double*>(data);
    if (indptr_type == dtype_int32) {
      const int32_t* ptr_indptr = reinterpret_cast<const int32_t*>(indptr);
      return [ptr_indptr, indices, data_ptr, nindptr, nelem](int idx) {
        CHECK(idx + 1 < nindptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_indptr[idx];
        int64_t end = ptr_indptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i <= end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else if (indptr_type == dtype_int64) {
      const int64_t* ptr_indptr = reinterpret_cast<const int64_t*>(indptr);
      return [ptr_indptr, indices, data_ptr, nindptr, nelem](int idx) {
        CHECK(idx + 1 < nindptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_indptr[idx];
        int64_t end = ptr_indptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i <= end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else {
      Log::Fatal("unknown data type in RowFunctionFromCSR");
    }
  } else {
    Log::Fatal("unknown data type in RowFunctionFromCSR");
  }
  return nullptr;
}

std::function<std::vector<std::pair<int, double>>(int idx)>
ColumnFunctionFromCSC(const void* col_ptr, int col_ptr_type, const int32_t* indices, const void* data, int data_type, int64_t ncol_ptr, int64_t nelem) {
  if (data_type == dtype_float32) {
    const float* data_ptr = reinterpret_cast<const float*>(data);
    if (col_ptr_type == dtype_int32) {
      const int32_t* ptr_col_ptr = reinterpret_cast<const int32_t*>(col_ptr);
      return [ptr_col_ptr, indices, data_ptr, ncol_ptr, nelem](int idx) {
        CHECK(idx + 1 < ncol_ptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_col_ptr[idx];
        int64_t end = ptr_col_ptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i < end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else if (col_ptr_type == dtype_int64) {
      const int64_t* ptr_col_ptr = reinterpret_cast<const int64_t*>(col_ptr);
      return [ptr_col_ptr, indices, data_ptr, ncol_ptr, nelem](int idx) {
        CHECK(idx + 1 < ncol_ptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_col_ptr[idx];
        int64_t end = ptr_col_ptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i < end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else {
      Log::Fatal("unknown data type in ColumnFunctionFromCSC");
    }
  } else if (data_type == dtype_float64) {
    const double* data_ptr = reinterpret_cast<const double*>(data);
    if (col_ptr_type == dtype_int32) {
      const int32_t* ptr_col_ptr = reinterpret_cast<const int32_t*>(col_ptr);
      return [ptr_col_ptr, indices, data_ptr, ncol_ptr, nelem](int idx) {
        CHECK(idx + 1 < ncol_ptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_col_ptr[idx];
        int64_t end = ptr_col_ptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i < end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else if (col_ptr_type == dtype_int64) {
      const int64_t* ptr_col_ptr = reinterpret_cast<const int64_t*>(col_ptr);
      return [ptr_col_ptr, indices, data_ptr, ncol_ptr, nelem](int idx) {
        CHECK(idx + 1 < ncol_ptr);
        std::vector<std::pair<int, double>> ret;
        int64_t start = ptr_col_ptr[idx];
        int64_t end = ptr_col_ptr[idx + 1];
        CHECK(start >= 0 && end <= nelem);
        for (int64_t i = start; i < end; ++i) {
          ret.emplace_back(indices[i], data_ptr[i]);
        }
        return ret;
      };
    } else {
      Log::Fatal("unknown data type in ColumnFunctionFromCSC");
    }
  } else {
    Log::Fatal("unknown data type in ColumnFunctionFromCSC");
  }
  return nullptr;
}

std::vector<double> SampleFromOneColumn(const std::vector<std::pair<int, double>>& data, const std::vector<size_t>& indices) {
  size_t j = 0;
  std::vector<double> ret;
  for (auto row_idx : indices) {
    while (j < data.size() && data[j].first < static_cast<int>(row_idx)) {
      ++j;
    }
    if (j < data.size() && data[j].first == static_cast<int>(row_idx)) {
      ret.push_back(data[j].second);
    } else {
      ret.push_back(0);
    }
  }
  return ret;
}