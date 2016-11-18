#ifndef LIGHTGBM_TREE_LEARNER_H_
#define LIGHTGBM_TREE_LEARNER_H_


#include <LightGBM/meta.h>
#include <LightGBM/config.h>

#include <vector>

namespace LightGBM {

/*! \brief forward declaration */
class Tree;
class Dataset;

/*!
* \brief Interface for tree learner
*/
class TreeLearner {
public:
  /*! \brief virtual destructor */
  virtual ~TreeLearner() {}

  /*!
  * \brief Initialize tree learner with training dataset and configs
  * \param train_data The used training data
  */
  virtual void Init(const Dataset* train_data) = 0;

  /*!
  * \brief training tree model on dataset 
  * \param gradients The first order gradients
  * \param hessians The second order gradients
  * \return A trained tree
  */
  virtual Tree* Train(const score_t* gradients, const score_t* hessians) = 0;

  /*!
  * \brief Set bagging data
  * \param used_indices Used data indices
  * \param num_data Number of used data
  */
  virtual void SetBaggingData(const data_size_t* used_indices,
    data_size_t num_data) = 0;

  /*!
  * \brief Using last trained tree to predict score then adding to out_score;
  * \param out_score output score
  */
  virtual void AddPredictionToScore(score_t *out_score) const = 0;

  TreeLearner() = default;
  /*! \brief Disable copy */
  TreeLearner& operator=(const TreeLearner&) = delete;
  /*! \brief Disable copy */
  TreeLearner(const TreeLearner&) = delete;

  /*!
  * \brief Create object of tree learner
  * \param type Type of tree learner
  */
  static TreeLearner* CreateTreeLearner(TreeLearnerType type,
    const TreeConfig& tree_config);
};

}  // namespace LightGBM

#endif   // LightGBM_TREE_LEARNER_H_
