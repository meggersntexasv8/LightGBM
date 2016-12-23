# coding: utf-8
# pylint: skip-file
import os, unittest, math
import numpy as np
import lightgbm as lgb
from sklearn.metrics import log_loss, mean_squared_error, mean_absolute_error
from sklearn.datasets import load_breast_cancer, load_boston, load_digits, load_iris
from sklearn.model_selection import train_test_split

def multi_logloss(y_true, y_pred):
    return np.mean([-math.log(y_pred[i][y]) for i, y in enumerate(y_true)])

def test_template(params = {'objective' : 'regression', 'metric' : 'l2'},
                X_y=load_boston(True), feval=mean_squared_error,
                stratify=None, num_round=100, return_data=False,
                return_model=False, init_model=None, custom_eval=None):
    X, y = X_y
    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.1,
                                                        stratify=stratify,
                                                        random_state=42)
    lgb_train = lgb.Dataset(X_train, y_train, free_raw_data=not return_model, params=params)
    lgb_eval = lgb.Dataset(X_test, y_test, reference=lgb_train, free_raw_data=not return_model, params=params)
    if return_data: return lgb_train, lgb_eval
    evals_result = {}
    params['verbose'] = 0
    gbm = lgb.train(params, lgb_train,
                    num_boost_round=num_round,
                    valid_sets=lgb_eval,
                    valid_names='eval',
                    verbose_eval=False,
                    feval=custom_eval,
                    evals_result=evals_result,
                    early_stopping_rounds=10,
                    init_model=init_model)
    if return_model: return gbm
    else: return evals_result, feval(y_test, gbm.predict(X_test, gbm.best_iteration))

class TestBasic(unittest.TestCase):

    def test_binary(self):
        X_y= load_breast_cancer(True)
        params = {
            'objective' : 'binary',
            'metric' : 'binary_logloss'
        }
        evals_result, ret = test_template(params, X_y, log_loss, stratify=X_y[1])
        self.assertLess(ret, 0.15)
        self.assertAlmostEqual(min(evals_result['eval']['logloss']), ret, places=5)
    
    def test_regreesion(self):
        evals_result, ret = test_template()
        ret **= 0.5
        self.assertLess(ret, 4)
        self.assertAlmostEqual(min(evals_result['eval']['l2']), ret, places=5)

    def test_multiclass(self):
        X_y = load_digits(10, True)
        params = {
            'objective' : 'multiclass',
            'metric' : 'multi_logloss',
            'num_class' : 10
        }
        evals_result, ret = test_template(params, X_y, multi_logloss, stratify=X_y[1])
        self.assertLess(ret, 0.2)
        self.assertAlmostEqual(min(evals_result['eval']['multi_logloss']), ret, places=5)

    def test_continue_train_and_other(self):
        params = {
            'objective' : 'regression',
            'metric' : 'l1'
        }
        model_name = 'model.txt'
        gbm = test_template(params, num_round=20, return_model=True)
        gbm.save_model(model_name)
        evals_result, ret = test_template(params, feval=mean_absolute_error,
                                        num_round=80, init_model=model_name,
                                        custom_eval=(lambda p, d: ('mae', mean_absolute_error(p, d.get_label()), False)))
        self.assertLess(ret, 3)
        self.assertAlmostEqual(min(evals_result['eval']['l1']), ret, places=5)
        for l1, mae in zip(evals_result['eval']['l1'], evals_result['eval']['mae']):
            self.assertAlmostEqual(l1, mae, places=5)
        self.assertIn('tree_info', gbm.dump_model())
        self.assertIsInstance(gbm.feature_importance(), np.ndarray)
        os.remove(model_name)

    def test_continue_train_multiclass(self):
        X_y = load_iris(True)
        params = {
            'objective' : 'multiclass',
            'metric' : 'multi_logloss',
            'num_class' : 3
        }
        gbm = test_template(params, X_y, num_round=20, return_model=True, stratify=X_y[1])
        evals_result, ret = test_template(params, X_y, feval=multi_logloss,
                                        num_round=80, init_model=gbm)
        self.assertLess(ret, 1.5)
        self.assertAlmostEqual(min(evals_result['eval']['multi_logloss']), ret, places=5)

    def test_cv(self):
        lgb_train, lgb_eval = test_template(return_data=True)
        lgb.cv({'verbose':0}, lgb_train, num_boost_round=200, nfold=5,
                metrics='l1', verbose_eval=False)

print("----------------------------------------------------------------------")
print("running test_engine.py")
unittest.main()
