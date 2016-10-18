#ifndef LIGHTGBM_CONFIG_H_
#define LIGHTGBM_CONFIG_H_

#include <LightGBM/utils/common.h>
#include <LightGBM/utils/log.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <algorithm>

namespace LightGBM {

/*!
* \brief The interface for Config
*/
struct ConfigBase {
public:
  /*! \brief virtual destructor */
  virtual ~ConfigBase() {}

  /*!
  * \brief SetLabelAt current config object by params
  * \param params Store the key and value for params
  */
  virtual void Set(
    const std::unordered_map<std::string, std::string>& params) = 0;

  /*!
  * \brief Get string value by specific name of key
  * \param params Store the key and value for params
  * \param name Name of key
  * \param out Value will asign to out if key exists
  * \return True if key exists
  */
  inline bool GetString(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name, std::string* out);

  /*!
  * \brief Get int value by specific name of key
  * \param params Store the key and value for params
  * \param name Name of key
  * \param out Value will asign to out if key exists
  * \return True if key exists
  */
  inline bool GetInt(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name, int* out);

  /*!
  * \brief Get double value by specific name of key
  * \param params Store the key and value for params
  * \param name Name of key
  * \param out Value will asign to out if key exists
  * \return True if key exists
  */
  inline bool GetDouble(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name, double* out);

  /*!
  * \brief Get bool value by specific name of key
  * \param params Store the key and value for params
  * \param name Name of key
  * \param out Value will asign to out if key exists
  * \return True if key exists
  */
  inline bool GetBool(
    const std::unordered_map<std::string, std::string>& params,
    const std::string& name, bool* out);
};

/*! \brief Types of boosting */
enum BoostingType {
  kGBDT
};


/*! \brief Types of tasks */
enum TaskType {
  kTrain, kPredict
};

/*! \brief Config for input and output files */
struct IOConfig: public ConfigBase {
public:
  int max_bin = 255;
  int data_random_seed = 1;
  std::string data_filename = "";
  std::vector<std::string> valid_data_filenames;
  std::string output_model = "LightGBM_model.txt";
  std::string output_result = "LightGBM_predict_result.txt";
  std::string input_model = "";
  std::string input_init_score = "";
  int num_model_predict = -1;
  bool is_pre_partition = false;
  bool is_enable_sparse = true;
  bool use_two_round_loading = false;
  bool is_save_binary_file = false;
  bool is_sigmoid = true;
  void Set(const std::unordered_map<std::string, std::string>& params) override;
};

/*! \brief Config for objective function */
struct ObjectiveConfig: public ConfigBase {
public:
  virtual ~ObjectiveConfig() {}
  double sigmoid = 1;
  // for lambdarank
  std::vector<double> label_gain;
  // for lambdarank
  int max_position = 20;
  // for binary
  bool is_unbalance = false;
  void Set(const std::unordered_map<std::string, std::string>& params) override;
};

/*! \brief Config for metrics interface*/
struct MetricConfig: public ConfigBase {
public:
  virtual ~MetricConfig() {}
  int output_freq = 1;
  double sigmoid = 1;
  bool is_provide_training_metric = false;
  std::vector<double> label_gain;
  std::vector<int> eval_at;
  void Set(const std::unordered_map<std::string, std::string>& params) override;
};


/*! \brief Config for tree model */
struct TreeConfig: public ConfigBase {
public:
  int min_data_in_leaf = 100;
  double min_sum_hessian_in_leaf = 10.0f;
  int num_leaves = 127;
  int feature_fraction_seed = 2;
  double feature_fraction = 1.0;
  void Set(const std::unordered_map<std::string, std::string>& params) override;
};

/*! \brief Types of tree learning algorithms */
enum TreeLearnerType {
  kSerialTreeLearner, kFeatureParallelTreelearner,
  kDataParallelTreeLearner
};

/*! \brief Config for Boosting */
struct BoostingConfig: public ConfigBase {
public:
  virtual ~BoostingConfig() {}
  int num_iterations = 10;
  double learning_rate = 0.1;
  double bagging_fraction = 1.0;
  int bagging_seed = 3;
  int bagging_freq = 0;
  void Set(const std::unordered_map<std::string, std::string>& params) override;
};

/*! \brief Config for GBDT */
struct GBDTConfig: public BoostingConfig {
public:
  TreeLearnerType tree_learner_type = TreeLearnerType::kSerialTreeLearner;
  TreeConfig tree_config;
  void Set(const std::unordered_map<std::string, std::string>& params) override;

private:
  void GetTreeLearnerType(const std::unordered_map<std::string,
                                         std::string>& params);
};

/*! \brief Config for Network */
struct NetworkConfig: public ConfigBase {
public:
  int num_machines = 1;
  int local_listen_port = 12400;
  int time_out = 120;  // in minutes
  std::string machine_list_filename = "";
  void Set(const std::unordered_map<std::string, std::string>& params) override;
};


/*! \brief Overall config, all configs will put on this class */
struct OverallConfig: public ConfigBase {
public:
  TaskType task_type = TaskType::kTrain;
  NetworkConfig network_config;
  int num_threads = 0;
  bool is_parallel = false;
  bool is_parallel_find_bin = false;
  IOConfig io_config;
  BoostingType boosting_type = BoostingType::kGBDT;
  BoostingConfig* boosting_config;
  std::string objective_type = "regression";
  ObjectiveConfig objective_config;
  std::vector<std::string> metric_types;
  MetricConfig metric_config;
  ~OverallConfig() {
    delete boosting_config;
  }
  void Set(const std::unordered_map<std::string, std::string>& params) override;

private:
  void GetBoostingType(const std::unordered_map<std::string, std::string>& params);

  void GetObjectiveType(const std::unordered_map<std::string, std::string>& params);

  void GetMetricType(const std::unordered_map<std::string, std::string>& params);

  void GetTaskType(const std::unordered_map<std::string, std::string>& params);

  void CheckParamConflict();
};


inline bool ConfigBase::GetString(
  const std::unordered_map<std::string, std::string>& params,
  const std::string& name, std::string* out) {
  if (params.count(name) > 0) {
    *out = params.at(name);
    return true;
  }
  return false;
}

inline bool ConfigBase::GetInt(
  const std::unordered_map<std::string, std::string>& params,
  const std::string& name, int* out) {
  if (params.count(name) > 0) {
    Common::Atoi(params.at(name).c_str(), out);
    return true;
  }
  return false;
}

inline bool ConfigBase::GetDouble(
  const std::unordered_map<std::string, std::string>& params,
  const std::string& name, double* out) {
  if (params.count(name) > 0) {
    Common::Atof(params.at(name).c_str(), out);
    return true;
  }
  return false;
}

inline bool ConfigBase::GetBool(
  const std::unordered_map<std::string, std::string>& params,
  const std::string& name, bool* out) {
  if (params.count(name) > 0) {
    std::string value = params.at(name);
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    if (value == std::string("false")) {
      *out = false;
    } else {
      *out = true;
    }
    return true;
  }
  return false;
}

struct ParameterAlias {
  static void KeyAliasTransform(std::unordered_map<std::string, std::string>* params) {
    std::unordered_map<std::string, std::string> alias_table(
    {
      { "config", "config_file" },
      { "nthread", "num_threads" },
      { "num_thread", "num_threads" },
      { "boosting", "boosting_type" },
      { "boost", "boosting_type" },
      { "application", "objective" },
      { "app", "objective" },
      { "train_data", "data" },
      { "train", "data" },
      { "model_output", "output_model" },
      { "model_out", "output_model" },
      { "model_input", "input_model" },
      { "model_in", "input_model" },
      { "init_score", "input_init_score"},
      { "predict_result", "output_result" },
      { "prediction_result", "output_result" },
      { "valid", "valid_data" },
      { "test_data", "valid_data" },
      { "test", "valid_data" },
      { "is_sparse", "is_enable_sparse" },
      { "tranining_metric", "is_training_metric" },
      { "train_metric", "is_training_metric" },
      { "ndcg_at", "ndcg_eval_at" },
      { "min_data_per_leaf", "min_data_in_leaf" },
      { "min_data", "min_data_in_leaf" },
      { "min_sum_hessian_per_leaf", "min_sum_hessian_in_leaf" },
      { "min_sum_hessian", "min_sum_hessian_in_leaf" },
      { "min_hessian", "min_sum_hessian_in_leaf" },
      { "num_leaf", "num_leaves" },
      { "sub_feature", "feature_fraction" },
      { "num_iteration", "num_iterations" },
      { "num_tree", "num_iterations" },
      { "num_round", "num_iterations" },
      { "num_trees", "num_iterations" },
      { "num_rounds", "num_iterations" },
      { "sub_row", "bagging_fraction" },
      { "shrinkage_rate", "learning_rate" },
      { "tree", "tree_learner" },
      { "num_machine", "num_machines" },
      { "local_port", "local_listen_port" },
      { "two_round_loading", "use_two_round_loading"},
      { "two_round", "use_two_round_loading" },
      { "mlist", "machine_list_file" },
      { "is_save_binary", "is_save_binary_file" },
      { "save_binary", "is_save_binary_file" }
    });
    std::unordered_map<std::string, std::string> tmp_map;
    for (const auto& pair : *params) {
      if (alias_table.count(pair.first) > 0) {
        tmp_map[alias_table[pair.first]] = pair.second;
      }
    }
    for (const auto& pair : tmp_map) {
      if (params->count(pair.first) == 0) {
        params->insert(std::make_pair(pair.first, pair.second));
      }
    }
  }
};

}   // namespace LightGBM

#endif   // LightGBM_CONFIG_H_
