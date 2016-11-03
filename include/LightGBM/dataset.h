#ifndef LIGHTGBM_DATA_H_
#define LIGHTGBM_DATA_H_

#include <LightGBM/utils/random.h>
#include <LightGBM/utils/text_reader.h>

#include <LightGBM/meta.h>
#include <LightGBM/config.h>

#include <vector>
#include <utility>
#include <functional>
#include <string>
#include <unordered_set>

namespace LightGBM {

/*! \brief forward declaration */
class Feature;

/*!
* \brief This class is used to store some meta(non-feature) data for training data,
*        e.g. labels, weights, initial scores, qurey level informations.
*
*        Some details:
*        1. Label, used for traning.
*        2. Weights, weighs of records, optional
*        3. Query Boundaries, necessary for lambdarank.
*           The documents of i-th query is in [ query_boundarise[i], query_boundarise[i+1] )
*        4. Query Weights, auto calculate by weights and query_boundarise(if both of them are existed)
*           the weight for i-th query is sum(query_boundarise[i] , .., query_boundarise[i+1]) / (query_boundarise[i + 1] -  query_boundarise[i+1])
*        5. Initial score. optional. if exsitng, the model will boost from this score, otherwise will start from 0.
*/
class Metadata {
public:
 /*!
  * \brief Null costructor
  */
  Metadata();
  /*!
  * \brief Initialization will load qurey level informations, since it is need for sampling data
  * \param data_filename Filename of data
  * \param init_score_filename Filename of initial score
  * \param num_class Number of classes
  */
  void Init(const char* data_filename, const char* init_score_filename, const int num_class);
  /*!
  * \brief Initialize, only load initial score
  * \param init_score_filename Filename of initial score
  * \param num_class Number of classes
  */
  void Init(const char* init_score_filename, const int num_class);
  /*!
  * \brief Initial with binary memory
  * \param memory Pointer to memory
  */
  void LoadFromMemory(const void* memory);
  /*! \brief Destructor */
  ~Metadata();

  /*!
  * \brief Initial work, will allocate space for label, weight(if exists) and query(if exists)
  * \param num_data Number of training data
  * \param num_class Number of classes
  * \param weight_idx Index of weight column, < 0 means doesn't exists
  * \param query_idx Index of query id column, < 0 means doesn't exists
  */
  void Init(data_size_t num_data, int num_class, int weight_idx, int query_idx);

  /*!
  * \brief Partition label by used indices
  * \param used_indices Indice of local used
  */
  void PartitionLabel(const std::vector<data_size_t>& used_indices);

  /*!
  * \brief Partition meta data according to local used indices if need
  * \param num_all_data Number of total training data, including other machines' data on parallel learning
  * \param used_data_indices Indices of local used training data
  */
  void CheckOrPartition(data_size_t num_all_data,
    const std::vector<data_size_t>& used_data_indices);

  /*!
  * \brief Set initial scores
  * \param init_score Initial scores, this class will manage memory for init_score.
  */
  void SetInitScore(const float* init_score, data_size_t len);


  /*!
  * \brief Save binary data to file
  * \param file File want to write
  */
  void SaveBinaryToFile(FILE* file) const;

  /*!
  * \brief Get sizes in byte of this object
  */
  size_t SizesInByte() const;

  /*!
  * \brief Get pointer of label
  * \return Pointer of label
  */
  inline const float* label() const { return label_; }

  /*!
  * \brief Set label for one record
  * \param idx Index of this record
  * \param value Label value of this record
  */
  inline void SetLabelAt(data_size_t idx, float value)
  {
    label_[idx] = value;
  }

  /*!
  * \brief Set Weight for one record
  * \param idx Index of this record
  * \param value Weight value of this record
  */
  inline void SetWeightAt(data_size_t idx, float value)
  {
    weights_[idx] = value;
  }

  /*!
  * \brief Set Query Id for one record
  * \param idx Index of this record
  * \param value Query Id value of this record
  */
  inline void SetQueryAt(data_size_t idx, data_size_t value)
  {
    queries_[idx] = static_cast<data_size_t>(value);
  }

  /*!
  * \brief Get weights, if not exists, will return nullptr
  * \return Pointer of weights
  */
  inline const float* weights()
            const { return weights_; }

  /*!
  * \brief Get data boundaries on queries, if not exists, will return nullptr
  *        we assume data will order by query, 
  *        the interval of [query_boundaris[i], query_boundaris[i+1])
  *        is the data indices for query i.
  * \return Pointer of data boundaries on queries
  */
  inline const data_size_t* query_boundaries()
           const { return query_boundaries_; }

  /*!
  * \brief Get Number of queries
  * \return Number of queries
  */
  inline const data_size_t num_queries() const { return num_queries_; }

  /*!
  * \brief Get weights for queries, if not exists, will return nullptr
  * \return Pointer of weights for queries
  */
  inline const float* query_weights() const { return query_weights_; }

  /*!
  * \brief Get initial scores, if not exists, will return nullptr
  * \return Pointer of initial scores
  */
  inline const float* init_score() const { return init_score_; }
  
  /*! \brief Load initial scores from file */
  void LoadInitialScore();

private:
  /*! \brief Load wights from file */
  void LoadWeights();
  /*! \brief Load query boundaries from file */
  void LoadQueryBoundaries();
  /*! \brief Load query wights */
  void LoadQueryWeights();
  /*! \brief Filename of current data */
  const char* data_filename_;
  /*! \brief Filename of initial scores */
  const char* init_score_filename_;
  /*! \brief Number of data */
  data_size_t num_data_;
  /*! \brief Number of classes */
  int num_class_;
  /*! \brief Number of weights, used to check correct weight file */
  data_size_t num_weights_;
  /*! \brief Label data */
  float* label_;
  /*! \brief Label data, int type */
  int16_t* label_int_;
  /*! \brief Weights data */
  float* weights_;
  /*! \brief Query boundaries */
  data_size_t* query_boundaries_;
  /*! \brief Query weights */
  float* query_weights_;
  /*! \brief Number of querys */
  data_size_t num_queries_;
  /*! \brief Number of Initial score, used to check correct weight file */
  data_size_t num_init_score_;
  /*! \brief Initial score */
  float* init_score_;
  /*! \brief Queries data */
  data_size_t* queries_;
};


/*! \brief Interface for Parser */
class Parser {
public:

  /*! \brief virtual destructor */
  virtual ~Parser() {}

  /*!
  * \brief Parse one line with label
  * \param str One line record, string format, should end with '\0'
  * \param out_features Output columns, store in (column_idx, values)
  * \param out_label Label will store to this if exists
  */
  virtual void ParseOneLine(const char* str,
    std::vector<std::pair<int, double>>* out_features, double* out_label) const = 0;

  /*!
  * \brief Create a object of parser, will auto choose the format depend on file
  * \param filename One Filename of data
  * \param num_features Pass num_features of this data file if you know, <=0 means don't know
  * \param label_idx index of label column
  * \return Object of parser
  */
  static Parser* CreateParser(const char* filename, bool has_header, int num_features, int label_idx);
};

using PredictFunction =
  std::function<std::vector<double>(const std::vector<std::pair<int, double>>&)>;

/*! \brief The main class of data set,
*          which are used to traning or validation
*/
class Dataset {
public:
  /*!
  * \brief Constructor
  * \param data_filename Filename of dataset
  * \param init_score_filename Filename of initial score
  * \param io_config configs for IO
  * \param predict_fun Used for initial model, will give a prediction score based on this function, then set as initial score
  */
  Dataset(const char* data_filename, const char* init_score_filename,
    const IOConfig& io_config, const PredictFunction& predict_fun);

  /*!
  * \brief Constructor
  * \param data_filename Filename of dataset
  * \param io_config configs for IO
  * \param predict_fun Used for initial model, will give a prediction score based on this function, then set as initial score
  */
  Dataset(const char* data_filename,
    const IOConfig& io_config, const PredictFunction& predict_fun)
    : Dataset(data_filename, "", io_config, predict_fun) {
  }

  /*! \brief Destructor */
  ~Dataset();

  /*!
  * \brief Load training data on parallel training
  * \param rank Rank of local machine
  * \param num_machines Total number of all machines
  * \param is_pre_partition True if data file is pre-partitioned
  * \param use_two_round_loading True if need to use two round loading
  */
  void LoadTrainData(int rank, int num_machines, bool is_pre_partition,
                                           bool use_two_round_loading);

  /*!
  * \brief Load training data on single machine training
  * \param use_two_round_loading True if need to use two round loading
  */
  inline void LoadTrainData(bool use_two_round_loading) {
    LoadTrainData(0, 1, false, use_two_round_loading);
  }

  /*!
  * \brief Load data and use bin mapper from other data set, general this function is used to extract feature for validation data
  * \param train_set Other loaded data set
  * \param use_two_round_loading True if need to use two round loading
  */
  void LoadValidationData(const Dataset* train_set, bool use_two_round_loading);

  /*!
  * \brief Save current dataset into binary file, will save to "filename.bin"
  */
  void SaveBinaryFile();

  /*!
  * \brief Get a feature pointer for specific index
  * \param i Index for feature
  * \return Pointer of feature
  */
  inline const Feature* FeatureAt(int i) const { return features_[i]; }

  /*!
  * \brief Get meta data pointer
  * \return Pointer of meta data
  */
  inline const Metadata& metadata() const { return metadata_; }

  /*! \brief Get Number of used features */
  inline int num_features() const { return num_features_; }

  /*! \brief Get Number of total features */
  inline int num_total_features() const { return num_total_features_; }

  /*! \brief Get the index of label column */
  inline int label_idx() const { return label_idx_; }

  /*! \brief Get names of current data set */
  inline std::vector<std::string> feature_names() const { return feature_names_; }

  /*! \brief Get Number of data */
  inline data_size_t num_data() const { return num_data_; }

  /*! \brief Disable copy */
  Dataset& operator=(const Dataset&) = delete;
  /*! \brief Disable copy */
  Dataset(const Dataset&) = delete;

private:
  /*!
  * \brief Load data content on memory. if num_machines > 1 and !is_pre_partition, will partition data
  * \param rank Rank of local machine
  * \param num_machines Total number of all machines
  * \param is_pre_partition True if data file is pre-partitioned
  */
  void LoadDataToMemory(int rank, int num_machines, bool is_pre_partition);

  /*!
  * \brief Sample data from memory, need load data to memory first
  * \param out_data Store the sampled data
  */
  void SampleDataFromMemory(std::vector<std::string>* out_data);

  /*!
  * \brief Sample data from file
  * \param rank Rank of local machine
  * \param num_machines Total number of all machines
  * \param is_pre_partition True if data file is pre-partitioned
  * \param out_data Store the sampled data
  */
  void SampleDataFromFile(int rank, int num_machines,
    bool is_pre_partition, std::vector<std::string>* out_data);

  /*!
  * \brief Get feature bin mapper from sampled data.
  * if num_machines > 1, differnt machines will construct bin mapper for different features, then have a global sync up
  * \param rank Rank of local machine
  * \param num_machines Total number of all machines
  */
  void ConstructBinMappers(int rank, int num_machines,
         const std::vector<std::string>& sample_data);

  /*! \brief Extract local features from memory */
  void ExtractFeaturesFromMemory();

  /*! \brief Extract local features from file */
  void ExtractFeaturesFromFile();

  /*! \brief Check can load from binary file */
  void CheckCanLoadFromBin();

  /*!
  * \brief Load data set from binary file
  * \param rank Rank of local machine
  * \param num_machines Total number of all machines
  * \param is_pre_partition True if data file is pre-partitioned
  */
  void LoadDataFromBinFile(int rank, int num_machines, bool is_pre_partition);

  /*! \brief Check this data set is null or not */
  void CheckDataset();

  /*! \brief Filename of data */
  const char* data_filename_;
  /*! \brief A reader class that can read text data */
  TextReader<data_size_t>* text_reader_;
  /*! \brief A parser class that can parse data */
  Parser* parser_;
  /*! \brief Store used features */
  std::vector<Feature*> features_;
  /*! \brief Mapper from real feature index to used index*/
  std::vector<int> used_feature_map_;
  /*! \brief Number of used features*/
  int num_features_;
  /*! \brief Number of total features*/
  int num_total_features_;
  /*! \brief Number of total data*/
  data_size_t num_data_;
  /*! \brief Number of classes*/
  int num_class_;
  /*! \brief Store some label level data*/
  Metadata metadata_;
  /*! \brief Random generator*/
  Random random_;
  /*! \brief The maximal number of bin that feature values will bucket in */
  int max_bin_;
  /*! \brief True if enable sparse */
  bool is_enable_sparse_;
  /*! \brief True if dataset is loaded from binary file */
  bool is_loading_from_binfile_;
  /*! \brief Number of global data, used for distributed learning */
  size_t global_num_data_ = 0;
  /*! \brief used to local used data indices */
  std::vector<data_size_t> used_data_indices_;
  /*! \brief prediction function for initial model */
  const PredictFunction& predict_fun_;
  /*! \brief index of label column */
  int label_idx_ = 0;
  /*! \brief index of weight column */
  int weight_idx_ = -1;
  /*! \brief index of group column */
  int group_idx_ = -1;
  /*! \brief Mapper from real feature index to used index*/
  std::unordered_set<int> ignore_features_;
  /*! \brief store feature names */
  std::vector<std::string> feature_names_;
};

}  // namespace LightGBM

#endif   // LightGBM_DATA_H_
