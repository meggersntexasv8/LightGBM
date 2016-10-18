#include <LightGBM/application.h>

#include <LightGBM/utils/common.h>
#include <LightGBM/utils/text_reader.h>

#include <LightGBM/network.h>
#include <LightGBM/dataset.h>
#include <LightGBM/boosting.h>
#include <LightGBM/objective_function.h>
#include <LightGBM/metric.h>

#include "predictor.hpp"

#include <omp.h>

#include <cstdio>
#include <ctime>

#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace LightGBM {

Application::Application(int argc, char** argv)
  :train_data_(nullptr), boosting_(nullptr), objective_fun_(nullptr) {
  LoadParameters(argc, argv);
  // set number of threads for openmp
  if (config_.num_threads > 0) {
    omp_set_num_threads(config_.num_threads);
  }
}

Application::~Application() {
  if (train_data_ != nullptr) { delete train_data_; }
  for (auto& data : valid_datas_) {
    if (data != nullptr) { delete data; }
  }
  valid_datas_.clear();
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
  if (config_.is_parallel) {
    Network::Dispose();
  }
}

void Application::LoadParameters(int argc, char** argv) {
  std::unordered_map<std::string, std::string> params;
  for (int i = 1; i < argc; ++i) {
    std::vector<std::string> tmp_strs = Common::Split(argv[i], '=');
    if (tmp_strs.size() == 2) {
      std::string key = Common::RemoveQuotationSymbol(Common::Trim(tmp_strs[0]));
      std::string value = Common::RemoveQuotationSymbol(Common::Trim(tmp_strs[1]));
      if (key.size() <= 0) {
        continue;
      }
      params[key] = value;
    }
    else {
      Log::Stdout("Warning: unknown parameter in command line: %s", argv[i]);
    }
  }
  // check for alias
  ParameterAlias::KeyAliasTransform(&params);
  // read parameters from config file
  if (params.count("config_file") > 0) {
    TextReader<size_t> config_reader(params["config_file"].c_str());
    config_reader.ReadAllLines();
    if (config_reader.Lines().size() > 0) {
      for (auto& line : config_reader.Lines()) {
        // remove str after #
        if (line.size() > 0 && std::string::npos != line.find_first_of("#")) {
          line.erase(line.find_first_of("#"));
        }
        line = Common::Trim(line);
        if (line.size() == 0) {
          continue;
        }
        std::vector<std::string> tmp_strs = Common::Split(line.c_str(), '=');
        if (tmp_strs.size() == 2) {
          std::string key = Common::RemoveQuotationSymbol(Common::Trim(tmp_strs[0]));
          std::string value = Common::RemoveQuotationSymbol(Common::Trim(tmp_strs[1]));
          if (key.size() <= 0) {
            continue;
          }
          // Command line have higher priority
          if (params.count(key) == 0) {
            params[key] = value;
          }
        }
        else {
          Log::Stdout("Warning: unknown parameter in config file: %s", line.c_str());
        }
      }
    } else {
      Log::Stdout("config file: %s doesn't exist, will ignore",
                                params["config_file"].c_str());
    }
  }
  // check for alias again
  ParameterAlias::KeyAliasTransform(&params);
  // load configs
  config_.Set(params);
  Log::Stdout("finished load parameters");
}

void Application::LoadData() {
  auto start_time = std::chrono::high_resolution_clock::now();
  // predition is needed if using input initial model(continued train)
  PredictFunction predict_fun = nullptr;
  Predictor* predictor = nullptr;
  // load init model
  if (config_.io_config.input_model.size() > 0) {
    LoadModel();
    if (boosting_->NumberOfSubModels() > 0) {
      predictor = new Predictor(boosting_, config_.io_config.is_sigmoid);
      predict_fun =
        [&predictor](const std::vector<std::pair<int, double>>& features) {
        return predictor->PredictRawOneLine(features);
      };
    }
  }
  // sync up random seed for data partition
  if (config_.is_parallel_find_bin) {
    config_.io_config.data_random_seed =
       GlobalSyncUpByMin<int>(config_.io_config.data_random_seed);
  }
  train_data_ = new Dataset(config_.io_config.data_filename.c_str(),
                         config_.io_config.input_init_score.c_str(),
                                          config_.io_config.max_bin,
                                 config_.io_config.data_random_seed,
                                 config_.io_config.is_enable_sparse,
                                                       predict_fun);
  // load Training data
  if (config_.is_parallel_find_bin) {
    // load data for parallel training
    train_data_->LoadTrainData(Network::rank(), Network::num_machines(),
                                     config_.io_config.is_pre_partition,
                               config_.io_config.use_two_round_loading);
  } else {
    // load data for single machine
    train_data_->LoadTrainData(config_.io_config.use_two_round_loading);
  }
  // need save binary file
  if (config_.io_config.is_save_binary_file) {
    train_data_->SaveBinaryFile();
  }
  // create training metric
  if (config_.metric_config.is_provide_training_metric) {
    for (auto metric_type : config_.metric_types) {
      Metric* metric =
        Metric::CreateMetric(metric_type, config_.metric_config);
      if (metric == nullptr) { continue; }
      metric->Init("training", train_data_->metadata(),
                              train_data_->num_data());
      train_metric_.push_back(metric);
    }
  }
  // Add validation data, if exists
  for (size_t i = 0; i < config_.io_config.valid_data_filenames.size(); ++i) {
    // add
    valid_datas_.push_back(
      new Dataset(config_.io_config.valid_data_filenames[i].c_str(),
                                          config_.io_config.max_bin,
                                 config_.io_config.data_random_seed,
                                 config_.io_config.is_enable_sparse,
                                                      predict_fun));
    // load validation data like train data
    valid_datas_.back()->LoadValidationData(train_data_,
                config_.io_config.use_two_round_loading);
    // need save binary file
    if (config_.io_config.is_save_binary_file) {
      valid_datas_.back()->SaveBinaryFile();
    }

    // add metric for validation data
    valid_metrics_.emplace_back();
    for (auto metric_type : config_.metric_types) {
      Metric* metric = Metric::CreateMetric(metric_type, config_.metric_config);
      if (metric == nullptr) { continue; }
      metric->Init(config_.io_config.valid_data_filenames[i].c_str(),
                                     valid_datas_.back()->metadata(),
                                    valid_datas_.back()->num_data());
      valid_metrics_.back().push_back(metric);
    }
  }
  if (predictor != nullptr) {
    delete predictor;
  }
  auto end_time = std::chrono::high_resolution_clock::now();
  // output used time on each iteration
  Log::Stdout("Finish loading data, use %f seconds ",
    std::chrono::duration<double, std::milli>(end_time - start_time) * 1e-3);
}

void Application::InitTrain() {
  if (config_.is_parallel) {
    // need init network
    Network::Init(config_.network_config);
    Log::Stdout("finish network initialization");
    // sync global random seed for feature patition
    if (config_.boosting_type == BoostingType::kGBDT) {
      GBDTConfig* gbdt_config =
        dynamic_cast<GBDTConfig*>(config_.boosting_config);
      gbdt_config->tree_config.feature_fraction_seed =
        GlobalSyncUpByMin<int>(gbdt_config->tree_config.feature_fraction_seed);
      gbdt_config->tree_config.feature_fraction =
        GlobalSyncUpByMin<double>(gbdt_config->tree_config.feature_fraction);
    }
  }
  // create boosting
  boosting_ =
    Boosting::CreateBoosting(config_.boosting_type, config_.boosting_config);
  // create objective function
  objective_fun_ =
    ObjectiveFunction::CreateObjectiveFunction(config_.objective_type,
                                             config_.objective_config);
  // load training data
  LoadData();
  // initialize the objective function
  objective_fun_->Init(train_data_->metadata(), train_data_->num_data());
  // initialize the boosting
  boosting_->Init(train_data_, objective_fun_,
    ConstPtrInVectorWarpper<Metric>(train_metric_),
            config_.io_config.output_model.c_str());
  // add validation data into boosting
  for (size_t i = 0; i < valid_datas_.size(); ++i) {
    boosting_->AddDataset(valid_datas_[i],
      ConstPtrInVectorWarpper<Metric>(valid_metrics_[i]));
  }
  Log::Stdout("finish training init");
}

void Application::Train() {
  Log::Stdout("start train");
  boosting_->Train();
  Log::Stdout("finish train");
}


void Application::Predict() {
  // create predictor
  Predictor predictor(boosting_, config_.io_config.is_sigmoid);
  predictor.Predict(config_.io_config.data_filename.c_str(), config_.io_config.output_result.c_str());
  Log::Stdout("finish predict");
}

void Application::InitPredict() {
  boosting_ =
    Boosting::CreateBoosting(config_.boosting_type, config_.boosting_config);
  LoadModel();
  Log::Stdout("finish predict init");
}

void Application::LoadModel() {
  TextReader<size_t> model_reader(config_.io_config.input_model.c_str());
  model_reader.ReadAllLines();
  std::stringstream ss;
  for (auto& line : model_reader.Lines()) {
    ss << line << '\n';
  }
  boosting_->ModelsFromString(ss.str(), config_.io_config.num_model_predict);
}

template<typename T>
T Application::GlobalSyncUpByMin(T& local) {
  T global = local;
  if (!config_.is_parallel) {
    // not need to sync if not parallel learning
    return global;
  }
  Network::Allreduce(reinterpret_cast<char*>(&local),
                         sizeof(local), sizeof(local),
                     reinterpret_cast<char*>(&global),
              [](const char* src, char* dst, int len) {
    int used_size = 0;
    const int type_size = sizeof(T);
    const T *p1;
    T *p2;
    while (used_size < len) {
      p1 = reinterpret_cast<const T *>(src);
      p2 = reinterpret_cast<T *>(dst);
      if (*p1 < *p2) {
        std::memcpy(dst, src, type_size);
      }
      src += type_size;
      dst += type_size;
      used_size += type_size;
    }
  });
  return global;
}

}  // namespace LightGBM
