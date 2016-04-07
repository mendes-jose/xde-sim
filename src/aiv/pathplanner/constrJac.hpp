#include <Eigen/Dense>
#include "aiv/pathplanner/FlatoutputMonocycle.hpp"

typedef Eigen::Matrix<double, aiv::FlatoutputMonocycle::poseDim, Eigen::Dynamic> qJacMatrix;
typedef Eigen::Matrix<double, aiv::FlatoutputMonocycle::velocityDim, Eigen::Dynamic> dqJacMatrix;
typedef Eigen::Matrix<double, aiv::FlatoutputMonocycle::accelerationDim, Eigen::Dynamic> ddqJacMatrix;
typedef Eigen::Matrix<double, 1, Eigen::Dynamic> obstDistJacMatrix;

qJacMatrix qJac(const double t, const double Tp, const int span, const double *X, const int Xdim, const int spld);
dqJacMatrix dqJac(const double t, const double Tp, const int span, const double *X, const int Xdim, const int spld);
dqJacMatrix absdqJac(const double t, const double Tp, const int span, const double *X, const int Xdim, const int spld);
ddqJacMatrix ddqJac(const double t, const double Tp, const int span, const double *X, const int Xdim, const int spld);
ddqJacMatrix absddqJac(const double t, const double Tp, const int span, const double *X, const int Xdim, const int spld);
obstDistJacMatrix obstDistJac(const double t, const double Tp, const int span, const double *X, const int Xdim, const int spld, Eigen::Vector2d obstPosition);

// cmake:sourcegroup=PathPlanner