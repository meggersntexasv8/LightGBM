#ifndef LIGHTGBM_PREDICTOR_HPP_
#define LIGHTGBM_PREDICTOR_HPP_

#include <LightGBM/meta.h>
#include <LightGBM/boosting.h>
#include <LightGBM/utils/text_reader.h>
#include <LightGBM/dataset.h>

#include <omp.h>

#include <cstring>
#include <cstdio>
#include <vector>
#include <utility>
#include <functional>
#include <string>

namespace LightGBM {

/*!
* \brief Used to prediction data with input model
*/
class Predictor {
public:
  /*!
  * \brief Constructor
  * \param boosting Input boosting model
  * \param is_sigmoid True if need to predict result with sigmoid transform (if needed, like binary classification)
  * \param predict_leaf_index True if output leaf index instead of prediction score
  */
  Predictor(const Boosting* boosting, bool is_simgoid, bool is_predict_leaf_index)
    : is_simgoid_(is_simgoid), is_predict_leaf_index_(is_predict_leaf_index) {
    boosting_ = boosting;
    num_features_ = boosting_->MaxFeatureIdx() + 1;
    num_class_ = boosting_->NumberOfClass();
#pragma omp parallel
#pragma omp master
    {
      num_threads_ = omp_get_num_threads();
    }
    features_ = new double*[num_threads_];
    for (int i = 0; i < num_threads_; ++i) {
      features_[i] = new double[num_features_];
    }
  }
  /*!
  * \brief Destructor
  */
  ~Predictor() {
    if (features_ != nullptr) {
      for (int i = 0; i < num_threads_; ++i) {
        delete[] features_[i];
      }
      delete[] features_;
    }
  }

  /*!
  * \brief prediction for one record, only raw result (without sigmoid transformation)
  * \param features Feature for this record
  * \return Prediction result
  */
  std::vector<double> PredictRawOneLine(const std::vector<std::pair<int, double>>& features) {
    const int tid = PutFeatureValuesToBuffer(features);
    // get result without sigmoid transformation
    return std::vector<double>(1, boosting_->PredictRaw(features_[tid]));
  }

  /*!
  * \brief prediction for one record, only raw result (without sigmoid transformation)
  * \param features Feature for this record
  * \return Predictied leaf index
  */
  std::vector<int> PredictLeafIndexOneLine(const std::vector<std::pair<int, double>>& features) {
    const int tid = PutFeatureValuesToBuffer(features);
    // get result for leaf index
    return boosting_->PredictLeafIndex(features_[tid]);
  }

  /*!
  * \brief prediction for one record, will use sigmoid transformation if needed (only enabled for binary classification noe)
  * \param features Feature of this record
  * \return Prediction result
  */
  std::vector<double> PredictOneLine(const std::vector<std::pair<int, double>>& features) {
    const int tid = PutFeatureValuesToBuffer(features);
    // get result with sigmoid transform if needed
    return std::vector<double>(1, boosting_->Predict(features_[tid]));
  }

  /*!
  * \brief prediction for multiclass classification
  * \param features Feature of this record
  * \return Prediction result
  */
  std::vector<double> PredictMulticlassOneLine(const std::vector<std::pair<int, double>>& features) {
    const int tid = PutFeatureValuesToBuffer(features);
    // get result with sigmoid transform if needed
    return boosting_->PredictMulticlass(features_[tid]);
  }

  /*!
  * \brief predicting on data, then saving result to disk
  * \param data_filename Filename of data
  * \param has_label True if this data contains label
  * \param result_filename Filename of output result
  */
  void Predict(const char* data_filename, const char* result_filename, bool has_header) {
    FILE* result_file;

#ifdef _MSC_VER
    fopen_s(&result_file, result_filename, "w");
#else
    result_file = fopen(result_filename, "w");
#endif

    if (result_file == NULL) {
      Log::Fatal("Prediction results file %s doesn't exist", data_filename);
    }
    Parser* parser = Parser::CreateParser(data_filename, has_header, num_features_, boosting_->LabelIdx());

    if (parser == nullptr) {
      Log::Fatal("Could not recognize the data format of data file %s", data_filename);
    }

    // function for parse data
    std::function<void(const char*, std::vector<std::pair<int, double>>*)> parser_fun;
    double tmp_label;
    parser_fun = [this, &parser, &tmp_label]
    (const char* buffer, std::vector<std::pair<int, double>>* feature) {
      parser->ParseOneLine(buffer, feature, &tmp_label);
    };

    std::function<std::string(const std::vector<std::pair<int, double>>&)> predict_fun;
    if (num_class_ > 1) {
      predict_fun = [this](const std::vector<std::pair<int, double>>& features){
        std::vector<double> prediction = PredictMulticlassOneLine(features);
        Common::Softmax(&prediction);
        std::stringstream result_stream_buf;
        for (size_t i = 0; i < prediction.size(); ++i){
          if (i > 0) {
            result_stream_buf << '\t';
          }
          result_stream_buf << prediction[i];
        }
        return result_stream_buf.str();
      };
    }
    else if (is_predict_leaf_index_) {
      predict_fun = [this](const std::vector<std::pair<int, double>>& features){
        std::vector<int> predicted_leaf_index = PredictLeafIndexOneLine(features);
        std::stringstream result_stream_buf;
        for (size_t i = 0; i < predicted_leaf_index.size(); ++i){
          if (i > 0) {
            result_stream_buf << '\t';
          }
          result_stream_buf << predicted_leaf_index[i];
        }
        return result_stream_buf.str();
      };
    }
    else {
      if (is_simgoid_) {
        predict_fun = [this](const std::vector<std::pair<int, double>>& features){
          return std::to_string(PredictOneLine(features)[0]);
        };
      }
      else {
        predict_fun = [this](const std::vector<std::pair<int, double>>& features){
          return std::to_string(PredictRawOneLine(features)[0]);
        };
      }
    }
    std::function<void(data_size_t, const std::vector<std::string>&)> process_fun =
      [this, &parser_fun, &predict_fun, &result_file]
    (data_size_t, const std::vector<std::string>& lines) {
      std::vector<std::pair<int, double>> oneline_features;
      std::vector<std::string> pred_result(lines.size(), "");
#pragma omp parallel for schedule(static) private(oneline_features)
      for (data_size_t i = 0; i < static_cast<data_size_t>(lines.size()); ++i) {
        oneline_features.clear();
        // parser
        parser_fun(lines[i].c_str(), &oneline_features);
        // predict
        pred_result[i] = predict_fun(oneline_features);
      }

      for (size_t i = 0; i < pred_result.size(); ++i) {
        fprintf(result_file, "%s\n", pred_result[i].c_str());
      }
    };
    TextReader<data_size_t> predict_data_reader(data_filename, has_header);
    predict_data_reader.ReadAllAndProcessParallel(process_fun);

    fclose(result_file);
    delete parser;
  }

private:
  int PutFeatureValuesToBuffer(const std::vector<std::pair<int, double>>& features) {
    int tid = omp_get_thread_num();
    // init feature value
    std::memset(features_[tid], 0, sizeof(double)*num_features_);
    // put feature value
    for (const auto& p : features) {
      if (p.first < num_features_) {
        features_[tid][p.first] = p.second;
      }
    }
    return tid;
  }
  /*! \brief Boosting model */
  const Boosting* boosting_;
  /*! \brief Buffer for feature values */
  double** features_;
  /*! \brief Number of features */
  int num_features_;
  /*! \brief Number of classes */
  int num_class_;
  /*! \brief True if need to predict result with sigmoid transform */
  bool is_simgoid_;
  /*! \brief Number of threads */
  int num_threads_;
  /*! \brief True if output leaf index instead of prediction score */
  bool is_predict_leaf_index_;
};

}  // namespace LightGBM

#endif   // LightGBM_PREDICTOR_HPP_
