/* ----------------------------------------------------------------------------

 * GTSAM Copyright 2010, Georgia Tech Research Corporation,
 * Atlanta, Georgia 30332-0415
 * All Rights Reserved
 * Authors: Frank Dellaert, et al. (see THANKS for the full author list)

 * See LICENSE for the license information

 * -------------------------------------------------------------------------- */

/**
 * @file    LevenbergMarquardtOptimizer.cpp
 * @brief   
 * @author  Richard Roberts
 * @created Feb 26, 2012
 */

#include <gtsam/nonlinear/LevenbergMarquardtOptimizer.h>

#include <gtsam/base/cholesky.h> // For NegativeMatrixException
#include <gtsam/linear/GaussianMultifrontalSolver.h>
#include <gtsam/linear/GaussianSequentialSolver.h>

using namespace std;

namespace gtsam {

/* ************************************************************************* */
NonlinearOptimizer::SharedState LevenbergMarquardtOptimizer::iterate(const NonlinearOptimizer::SharedState& _current) const {

  const LevenbergMarquardtState& current = dynamic_cast<const LevenbergMarquardtState&>(*_current);

  // Linearize graph
  const Ordering& ordering = this->ordering(current.values);
  GaussianFactorGraph::shared_ptr linear = graph_->linearize(current.values, ordering);

  // Check whether to use QR
  bool useQR;
  if(params_->factorization == LevenbergMarquardtParams::LDL)
    useQR = false;
  else if(params_->factorization == LevenbergMarquardtParams::QR)
    useQR = true;
  else
    throw runtime_error("Optimization parameter is invalid: LevenbergMarquardtParams::factorization");

  // Pull out parameters we'll use
  const NonlinearOptimizerParams::Verbosity nloVerbosity = params_->verbosity;
  const LevenbergMarquardtParams::LMVerbosity lmVerbosity = params_->lmVerbosity;
  const double lambdaFactor = params_->lambdaFactor;

  // Variables to update during try_lambda loop
  double lambda = current.lambda;
  double next_error = current.error;
  Values next_values = current.values;

  // Compute dimensions if we haven't done it yet
  if(!dimensions_)
    dimensions_.reset(new vector<size_t>(current.values.dims(ordering)));

  // Keep increasing lambda until we make make progress
  while(true) {
    if (lmVerbosity >= LevenbergMarquardtParams::TRYLAMBDA)
      cout << "trying lambda = " << lambda << endl;

    // Add prior-factors
    // TODO: replace this dampening with a backsubstitution approach
    GaussianFactorGraph dampedSystem(*linear);
    {
      double sigma = 1.0 / sqrt(lambda);
      dampedSystem.reserve(dampedSystem.size() + dimensions_->size());
      // for each of the variables, add a prior
      for(Index j=0; j<dimensions_->size(); ++j) {
        size_t dim = (*dimensions_)[j];
        Matrix A = eye(dim);
        Vector b = zero(dim);
        SharedDiagonal model = noiseModel::Isotropic::Sigma(dim,sigma);
        GaussianFactor::shared_ptr prior(new JacobianFactor(j, A, b, model));
        dampedSystem.push_back(prior);
      }
    }
    if (lmVerbosity >= LevenbergMarquardtParams::DAMPED) dampedSystem.print("damped");

    // Try solving
    try {

      // Optimize
      VectorValues::shared_ptr delta;
      if(params_->elimination == LevenbergMarquardtParams::MULTIFRONTAL)
        delta = GaussianMultifrontalSolver(dampedSystem, useQR).optimize();
      else if(params_->elimination == LevenbergMarquardtParams::SEQUENTIAL)
        delta = GaussianSequentialSolver(dampedSystem, useQR).optimize();
      else
        throw runtime_error("Optimization parameter is invalid: LevenbergMarquardtParams::elimination");

      if (lmVerbosity >= LevenbergMarquardtParams::TRYLAMBDA) cout << "linear delta norm = " << delta->vector().norm() << endl;
      if (lmVerbosity >= LevenbergMarquardtParams::TRYDELTA) delta->print("delta");

      // update values
      Values newValues = current.values.retract(*delta, ordering);

      // create new optimization state with more adventurous lambda
      double error = graph_->error(newValues);

      if (lmVerbosity >= LevenbergMarquardtParams::TRYLAMBDA) cout << "next error = " << error << endl;

      if(error <= current.error) {
        next_values.swap(newValues);
        next_error = error;
        lambda /= lambdaFactor;
        break;
      } else {
        // Either we're not cautious, or the same lambda was worse than the current error.
        // The more adventurous lambda was worse too, so make lambda more conservative
        // and keep the same values.
        if(lambda >= params_->lambdaUpperBound) {
          if(nloVerbosity >= NonlinearOptimizerParams::ERROR)
            cout << "Warning:  Levenberg-Marquardt giving up because cannot decrease error with maximum lambda" << endl;
          break;
        } else {
          lambda *= lambdaFactor;
        }
      }
    } catch(const NegativeMatrixException& e) {
      if(lmVerbosity >= LevenbergMarquardtParams::LAMBDA)
        cout << "Negative matrix, increasing lambda" << endl;
      // Either we're not cautious, or the same lambda was worse than the current error.
      // The more adventurous lambda was worse too, so make lambda more conservative
      // and keep the same values.
      if(lambda >= params_->lambdaUpperBound) {
        if(nloVerbosity >= NonlinearOptimizerParams::ERROR)
          cout << "Warning:  Levenberg-Marquardt giving up because cannot decrease error with maximum lambda" << endl;
        break;
      } else {
        lambda *= lambdaFactor;
      }
    } catch(...) {
      throw;
    }
  } // end while

  // Create new state with new values and new error
  LevenbergMarquardtOptimizer::SharedState newState = boost::make_shared<LevenbergMarquardtState>();
  newState->values = next_values;
  newState->error = next_error;
  newState->iterations = current.iterations + 1;
  newState->lambda = lambda;

  return newState;
}

/* ************************************************************************* */
NonlinearOptimizer::SharedState LevenbergMarquardtOptimizer::initialState(const Values& initialValues) const {
  SharedState initial = boost::make_shared<LevenbergMarquardtState>();
  defaultInitialState(initialValues, *initial);
  initial->lambda = params_->lambdaInitial;
  return initial;
}

} /* namespace gtsam */
