#ifndef __AIV_PATHPLANNERRECHOR_HPP__
#define __AIV_PATHPLANNERRECHOR_HPP__
#pragma once

#include "aiv/pathplanner/PathPlanner.hpp"
#include "aiv/pathplanner/FlatoutputMonocycle.hpp"
#include "aiv/pathplanner/Trajectory.hpp"
#include <boost/thread.hpp>
#include <fstream>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <set>

//Spline< dim, degree >

namespace aiv {

	typedef Eigen::Matrix< double, FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 1 > NDerivativesMatrix;
	typedef Eigen::Matrix< double, FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 2 > Np1DerivativesMatrix;
	typedef Eigen::Matrix< double, FlatoutputMonocycle::poseDim, 1 > PoseVector;
	typedef Eigen::Matrix< double, FlatoutputMonocycle::flatDim, 1 > FlatVector;
	typedef Eigen::Matrix< double, FlatoutputMonocycle::veloDim, 1 > VeloVector;
	typedef Eigen::Matrix< double, FlatoutputMonocycle::accelDim, 1 > AccelVector;

	class Obstacle;
	class AIV;

	class PathPlannerRecHor : public PathPlanner
	{

		/*____________________________________________ Members ____________________________________________*/
	public:
		static const unsigned splDegree = FlatoutputMonocycle::flatDerivDeg + 1; // spline degree defined by Defoort is = splDegree + 1    
		static const unsigned splDim = FlatoutputMonocycle::flatDim; // spline dimension
	private:

		// Trajectory representation ======================================================
		Trajectory _trajectory;
		Trajectory _optTrajectory; // For using by the optimization process
		//unsigned _nIntervNonNull;   // number of non null knots intervals
		//unsigned _nCtrlPts;         // number of control points

		// Initial and target conditions ==================================================
		PoseVector _initPose;
		FlatVector _initFlat;
		VeloVector _initVelocity;
		PoseVector _targetedPose;
		FlatVector _targetedFlat;
		VeloVector _targetedVelocity;
		VeloVector _maxVelocity;
		AccelVector _maxAcceleration;
		FlatVector _latestFlat;
		PoseVector _latestPose;
		VeloVector _latestVelocity;
		PoseVector _initPoseForFuturePlan; // buffer for base position
		PoseVector _initPoseForCurrPlan; // buffer for base position

		// Planning outputs ===============================================================
		VeloVector _velocityOutput;
		AccelVector _accelOutput;
		PoseVector _poseOutput;

		// Planning parameters ============================================================
		double _compHorizon;         // computation horizon
		double _planHorizon;      // planning horizon
		double _refPlanHorizon;      // planning horizon
		double _optPlanHorizon;    // planning horizon for using by the optimization process

		bool _planLastPlan; // last planning flag
		double _lastStepMinDist; // parameter for stop condition

		double _conflictFreePathDeviation;
		double _interRobotSafetyDist;
		double _robotObstacleSafetyDist;
		double _maxStraightDist;

		enum PlanStage { INIT, INTER, FINAL, DONE } _planStage;
		bool _waitForThread;

		//double _estTime;
		
		std::map< std::string, Obstacle* > _detectedObstacles;
		//std::map<std::string, std::vector<int> > conflictInfo;
		// std::map< std::string, AIV* > _collisionAIVs;
		// std::map< std::string, AIV* > _comOutAIVs;
		// std::set< std::string > _comAIVsSet;

		double _comRange;
		//double secRho; // secure distance from other robots and obstacles

		unsigned _nTimeSamples;     // number of  time samples taken within a planning horizon
		int _executingPlanIdx;

		Eigen::Matrix2d _rotMat2WRef;
		Eigen::Matrix2d _rotMat2RRef;
		FlatVector _wayPt;
		//int _ongoingPlanIdx;

		//enum ConflictEnum { NONE, COLL, COM, BOTH } _isThereConfl;

		// Optimization parameters ========================================================
		std::string _optimizerType;
		double _xTol;
		double _fTol;
		double _eqTol;
		double _ineqTol;
		unsigned _lastMaxIteration;
		unsigned _firstMaxIteration;
		unsigned _interMaxIteration;
		// double _h; // for numeric derivation

		// xde simulation related =========================================================
		double _updateTimeStep;
		double _firstPlanTimespan;
		//unsigned long long _updateCallCntr;
		double _currentPlanningTime;

		// multithreading management ======================================================
		boost::mutex _planOngoingMutex;
		boost::thread _planThread;


		// In out streamming ==============================================================
		// std::ofstream _opt_log;
		// std::ofstream _eq_log;
		boost::property_tree::ptree _property_tree;

		//double nbPointsCloseInTime;

		//double initTimeOfCurrPlan;


		/*____________________________________________ Methods ____________________________________________*/

	private:
		void _plan();
		//int findspan(const double t);
		void _solveOptPbl();
		//void _conflictEval(std::map<std::string, AIV *> otherVehicles, const Eigen::Displacementd & myRealPose);
		// int nbPointsCloseInTimeEval();

	public:
		PathPlannerRecHor(std::string name, double updateTimeStep);

		~PathPlannerRecHor();

		void init(
			const PoseVector &initPose, // x, y, theta
			const VeloVector &initVelocity, // v, w
			const PoseVector &targetedPose, // x, y, theta
			const VeloVector &targetedVelocity, // v, w
			const VeloVector &maxVelocity, // v, w
			double compHorizon,
			double refPlanHorizon,
			unsigned noTimeSamples,
			unsigned noIntervNonNull);

		void init(
			const PoseVector &initPose, // x, y, theta
			const VeloVector &initVelocity, // v, w
			const PoseVector &targetedPose, // x, y, theta
			const VeloVector &targetedVelocity, // v, w
			const VeloVector &maxVelocity, // v, w
			const VeloVector &maxAcceleration, // dv, dw
			double compHorizon,
			double refPlanHorizon,
			unsigned noTimeSamples,
			unsigned noIntervNonNull);

		void setOption(const std::string& optionName, const double optionValue);
		void setOption(const std::string& optionName, const bool optionValue);
		void setOption(const std::string& optionName, const unsigned optionValue);
		void setOption(const std::string& optionName, const std::string& optionValue);

		void update(std::map<std::string, Obstacle *> detectedObst,
				std::map<std::string, AIV *> otherVehicles,
				const Eigen::Displacementd & myRealPose); //const Eigen::Displacementd & realPose, const Eigen::Twistd &realVelocity);

		// getters
		inline double getLinVelocity() const { return _velocityOutput(FlatoutputMonocycle::linSpeedIdx); }
		inline double getMaxLinVelocity() const { return _maxVelocity(FlatoutputMonocycle::linSpeedIdx); }
		inline double getAngVelocity() const { return _velocityOutput(FlatoutputMonocycle::angSpeedIdx); }
		inline double getLinAccel() const { return _accelOutput(FlatoutputMonocycle::linSpeedIdx); }
		inline double getAngAccel() const { return _accelOutput(FlatoutputMonocycle::angSpeedIdx); }
		inline double getRobotObstacleSafetyDist() const { return _robotObstacleSafetyDist; }
		inline double getComRange() const { return _comRange; }

		inline double getXPosition() const { return _poseOutput(FlatoutputMonocycle::posIdx); }
		inline double getYPosition() const { return _poseOutput(FlatoutputMonocycle::posIdx + 1); }
		inline double getOrientation() const { return _poseOutput(FlatoutputMonocycle::posIdx + 2); }

		static double objectFunc(unsigned n, const double *x, double *grad, void *my_func_data);
		static double objectFuncLS(unsigned n, const double *x, double *grad, void *my_func_data);
		static void eqFunc(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data);
		static void eqFuncLS(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data);
		static void ineqFunc(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data);
		static void ineqFuncLS(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data);
		
		template<class T>
		static void evalObj(double *result, unsigned n, T x, PathPlannerRecHor *context);
		template<class T>
		static void evalEq(double *result, unsigned n, T x, PathPlannerRecHor *context);
		template<class T>
		static void evalIneq(double *result, unsigned n, T x, PathPlannerRecHor *context);

		void computeNumGrad(unsigned m, unsigned n, const double* x, double* grad, void (*eval)(double *, unsigned, volatile double*, PathPlannerRecHor*));
		//static void eval_eqHalf(double t, double *result, unsigned n, const double* x, void* data);
		//static void eval_ineq_coll(double *result, unsigned n, const double* x, void* data);
		// void eval_dobst_ineq(double *result, unsigned n, const double* x, void* data);
		// void ineqFuncColl(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data);
		// void eval_eqLS(double *result, unsigned n, const double* x, void* data);
		// void eval_ineqLS(double *result, unsigned n, const double* x, void* data);

		//inline bool isDone () {if (planStage == DONE) return true; else return false;}

		//inline double getInitTimeOfCurrPlan() const { return initTimeOfCurrPlan; }

		//inline MySpline getSpline() const { return auxSpline; }
		
		/*static double old_objectFunc(unsigned n, const double *x, double *grad, void *my_func_data);
		static void old_eqFunc(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data);
		static void old_ineqFunc(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data);*/
	};

	template<class T>
	void PathPlannerRecHor::evalObj(double *result, unsigned n, T x, PathPlannerRecHor *context)
	{
		// Update optimization trajectory with x
		context->_optTrajectory.update(x);

		// Create a Matrix consisting of the flat output and its needed derivatives for the instant Tp (1.0)
		NDerivativesMatrix derivFlat = context->_optTrajectory(context->_optPlanHorizon, FlatoutputMonocycle::flatDerivDeg);
		
		// Get pose at Tp from flatoutput
		PoseVector poseAtTp = FlatoutputMonocycle::flatToPose(derivFlat);

		// Position at Tp wrt robot at T0 to flat output wrt world frame of ref
		FlatVector flatAtTp = context->_rotMat2WRef*poseAtTp.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0) + context->_latestFlat;

		// Compute euclidian distance to square from position at Tp and goal position which will be the cost
		double fx = pow((context->_wayPt - flatAtTp).norm(), 2);

		*result = fx;
	}

	template<class T>
	void PathPlannerRecHor::evalEq(double *result, unsigned n, T x, PathPlannerRecHor *context)
	{
		// Update optimization trajectory with x
		context->_optTrajectory.update(x);

		// Create a Matrix consisting of the flat output and its needed derivatives for the instant T0 (0.0)
		NDerivativesMatrix derivFlatEq = context->_optTrajectory(0.0, FlatoutputMonocycle::flatDerivDeg);

		// Get error from pose at T0 and pose at the end of the previous plan (or initial position if this is the very first plan)
		//PoseVector poseAtT0 = FlatoutputMonocycle::flatToPose(derivFlatEq);
		PoseVector diffPoseAtT0 = FlatoutputMonocycle::flatToPose(derivFlatEq);
		//diffPoseAtT0.tail(1) -= context->_latestPose.tail(1);
		/*diffPoseAtT0.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0) =
			poseAtT0.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0);*/

		VeloVector diffVelocityAtT0 = FlatoutputMonocycle::flatToVelocity(derivFlatEq) - context->_latestVelocity;

		int i = 0;
		for (; i < FlatoutputMonocycle::poseDim; ++i)
		{
			result[i] = diffPoseAtT0[i];
			//std::cout << "G[" << i << "] " << g[i] << std::endl;
		}
		for (int j = i; j - i < FlatoutputMonocycle::veloDim; ++j)
		{
			result[j] = diffVelocityAtT0[j - i];
			//std::cout << "G[" << j << "] " << g[j] << std::endl;
		}
	}

	template<class T>
	void PathPlannerRecHor::evalIneq(double *result, unsigned n, T x, PathPlannerRecHor *context)
	{
		// Update optimization trajectory with x
		context->_optTrajectory.update(x);
		
		// INEQUATIONS

		Np1DerivativesMatrix derivFlat;
		NDerivativesMatrix derivFlatSmall;

		// Create Matrices for storing velocity and acceleration
		VeloVector velocity;
		AccelVector acceleration;

		PoseVector pose;

		derivFlat = context->_optTrajectory(0.0, FlatoutputMonocycle::flatDerivDeg + 1);


		// ACCELERATION AT 0.0
		acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

		int j = 0;
		for (; j < FlatoutputMonocycle::accelDim; ++j)
		{
			//const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
			result[j] = abs(acceleration(j,0)) - context->_maxAcceleration(j, 0);
			////std::cout << "G[" << nEq+j+(i-1)*ieqPerSample << "] " << g[nEq+j+(i-1)*ieqPerSample] << std::endl;
		}

		int nAcc = j;
		int ieqPerSample = (FlatoutputMonocycle::veloDim + FlatoutputMonocycle::accelDim + context->_detectedObstacles.size());

		//#pragma omp parallel for
		for (int i = 1; i <= int(context->_nTimeSamples); ++i)
		{
			////std::cout << "IEQ: SAMPLE IDX: " << i << std::endl;
			derivFlat = context->_optTrajectory(double(i) / context->_nTimeSamples * context->_optPlanHorizon, FlatoutputMonocycle::flatDerivDeg + 1);

			////std::cout << "got derive meatrix" << std::endl;
			// reusing derivFlatEq variable, ugly but let's move on
			derivFlatSmall = derivFlat.block<FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 1>(0, 0);

			////std::cout << "got vel" << std::endl;
			velocity = FlatoutputMonocycle::flatToVelocity(derivFlatSmall);

			pose = FlatoutputMonocycle::flatToPose(derivFlatSmall);

			////std::cout << "got acc" << std::endl;
			acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

			//int ieqPerSample = (FlatoutputMonocycle::veloDim);

			int j;
			for (j = 0; j < FlatoutputMonocycle::accelDim; ++j)
			{
				//const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
				result[nAcc + j + (i - 1)*ieqPerSample] = abs(acceleration(j,0)) - context->_maxAcceleration(j, 0);
				////std::cout << "G[" << nEq+j+(i-1)*ieqPerSample << "] " << g[nEq+j+(i-1)*ieqPerSample] << std::endl;
			}

			int k;
			for (k = j; k - j < FlatoutputMonocycle::veloDim; ++k)
			{
				//const double absVel = (velocity(k - j, 0) > -velocity(k - j, 0)) ? velocity(k - j, 0) : -velocity(k - j, 0);
				result[nAcc + k + (i - 1)*ieqPerSample] = abs(velocity(k - j, 0)) - context->_maxVelocity(k - j, 0);
				////std::cout << "G[" << nEq+k+(i-1)*ieqPerSample << "] " << g[nEq+k+(i-1)*ieqPerSample] << std::endl;
			}

			// Obstacles
			std::map<std::string, Obstacle *>::iterator it;
			for (it = context->_detectedObstacles.begin(), j = k; j - k < context->_detectedObstacles.size(); ++j, ++it)
			{
				result[nAcc + (i - 1)*ieqPerSample + j] =
					-1. * it->second->distToAIV(context->_rotMat2WRef*pose.head(2) + context->_latestPose.head(2), context->_robotObstacleSafetyDist);
			}
		}
	}

	template<class T>
	void PathPlannerRecHor::evalObjLS(double *result, unsigned n, T x, PathPlannerRecHor *context)
	{
		*result = x[0];
	}

	template<class T>
	void PathPlannerRecHor::evalEqLS(double *result, unsigned n, T x, PathPlannerRecHor *context)
	{
		// Update optimization trajectory with x
		context->_optTrajectory.update(&(x[1]), x[0]);

		// Create a Matrix consisting of the flat output and its needed derivatives for the instant T0 (0.0)
		NDerivativesMatrix derivFlatEq = context->_optTrajectory(0.0, FlatoutputMonocycle::flatDerivDeg);

		PoseVector diffPoseAtT0 = FlatoutputMonocycle::flatToPose(derivFlatEq);

		VeloVector diffVelocityAtT0 = FlatoutputMonocycle::flatToVelocity(derivFlatEq) - context->_latestVelocity;

		int i = 0;
		for (; i < FlatoutputMonocycle::poseDim; ++i)
		{
			result[i] = diffPoseAtT0[i];
			//std::cout << "G[" << i << "] " << g[i] << std::endl;
		}
		int j;
		for (j = i; j - i < FlatoutputMonocycle::veloDim; ++j)
		{
			result[j] = diffVelocityAtT0[j - i];
			//std::cout << "G[" << j << "] " << g[j] << std::endl;
		}

		// Create a Matrix consisting of the flat output and its needed derivatives for the instant T0 (0.0)
		NDerivativesMatrix derivFlatEq = context->_optTrajectory(x[0], FlatoutputMonocycle::flatDerivDeg);

		PoseVector poseAtTfWRTMySelf = FlatoutputMonocycle::flatToPose(derivFlatEq);
		//FIXME
		PoseVector diffPoseAtTf = (PoseVector() << context->_rotMat2WRef * poseAtTfWRTMySelf.top(2) + context->_latestFlat, poseAtTfWRTMySelf.tail(1)+context->_latestPose.tail(1)).finished() - context->_targetedPose;

		VeloVector diffVelocityAtTf = FlatoutputMonocycle::flatToVelocity(derivFlatEq) - context->_targetedVelocity;

		for (i = j; i - j < FlatoutputMonocycle::poseDim; ++i)
		{
			result[i] = diffPoseAtTf[i - j];
			//std::cout << "G[" << i << "] " << g[i] << std::endl;
		}
		for (j = i; j - i < FlatoutputMonocycle::veloDim; ++j)
		{
			result[j] = diffVelocityAtTf[j - i];
			//std::cout << "G[" << j << "] " << g[j] << std::endl;
		}
	}

	template<class T>
	void PathPlannerRecHor::evalIneqLS(double *result, unsigned n, T x, PathPlannerRecHor *context)
	{
		// Update optimization trajectory with x
		context->_optTrajectory.update(&(x[1]), x[0]);
		
		// INEQUATIONS
		Np1DerivativesMatrix derivFlat;
		NDerivativesMatrix derivFlatSmall;

		// Create Matrices for storing velocity and acceleration
		VeloVector velocity;
		AccelVector acceleration;
		PoseVector pose;

		// ACCELERATION AT 0.0
		derivFlat = context->_optTrajectory(0.0, FlatoutputMonocycle::flatDerivDeg + 1);

		acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

		int j = 0;
		for (; j < FlatoutputMonocycle::accelDim; ++j)
		{
			//const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
			result[j] = abs(acceleration(j,0)) - context->_maxAcceleration(j, 0);
			////std::cout << "G[" << nEq+j+(i-1)*ieqPerSample << "] " << g[nEq+j+(i-1)*ieqPerSample] << std::endl;
		}

		// ACCELERATION AT Tf
		derivFlat = context->_optTrajectory(x[0], FlatoutputMonocycle::flatDerivDeg + 1);

		acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

		int i;
		for (i = j; i - j < FlatoutputMonocycle::accelDim; ++i)
		{
			//const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
			result[i] = abs(acceleration(i,0)) - context->_maxAcceleration(i,0);
			////std::cout << "G[" << nEq+j+(i-1)*ieqPerSample << "] " << g[nEq+j+(i-1)*ieqPerSample] << std::endl;
		}

		int nAcc = i;
		// CONTINUE
		int ieqPerSample = (FlatoutputMonocycle::veloDim + FlatoutputMonocycle::accelDim + context->_detectedObstacles.size());

		//#pragma omp parallel for
		for (int i = 1; i <= int(context->_nTimeSamples); ++i)
		{
			////std::cout << "IEQ: SAMPLE IDX: " << i << std::endl;
			derivFlat = context->_optTrajectory(double(i) / context->_nTimeSamples * context->_optPlanHorizon, FlatoutputMonocycle::flatDerivDeg + 1);

			////std::cout << "got derive meatrix" << std::endl;
			// reusing derivFlatEq variable, ugly but let's move on
			derivFlatSmall = derivFlat.block<FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 1>(0, 0);

			////std::cout << "got vel" << std::endl;
			velocity = FlatoutputMonocycle::flatToVelocity(derivFlatSmall);

			pose = FlatoutputMonocycle::flatToPose(derivFlatSmall);

			////std::cout << "got acc" << std::endl;
			acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

			//int ieqPerSample = (FlatoutputMonocycle::veloDim);

			int j;
			for (j = 0; j < FlatoutputMonocycle::accelDim; ++j)
			{
				//const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
				result[nAcc + j + (i - 1)*ieqPerSample] = abs(acceleration(j,0)) - context->_maxAcceleration(j, 0);
				////std::cout << "G[" << nEq+j+(i-1)*ieqPerSample << "] " << g[nEq+j+(i-1)*ieqPerSample] << std::endl;
			}

			int k;
			for (k = j; k - j < FlatoutputMonocycle::veloDim; ++k)
			{
				//const double absVel = (velocity(k - j, 0) > -velocity(k - j, 0)) ? velocity(k - j, 0) : -velocity(k - j, 0);
				result[nAcc + k + (i - 1)*ieqPerSample] = abs(velocity(k - j, 0)) - context->_maxVelocity(k - j, 0);
				////std::cout << "G[" << nEq+k+(i-1)*ieqPerSample << "] " << g[nEq+k+(i-1)*ieqPerSample] << std::endl;
			}

			// Obstacles
			std::map<std::string, Obstacle *>::iterator it;
			for (it = context->_detectedObstacles.begin(), j = k; j - k < context->_detectedObstacles.size(); ++j, ++it)
			{
				result[nAcc + (i - 1)*ieqPerSample + j] =
					-1. * it->second->distToAIV(context->_rotMat2WRef*pose.head(2) + context->_latestPose.head(2), context->_robotObstacleSafetyDist);
			}
		}
	}


}

#endif // __AIV_PATHPLANNERRECHOR_HPP__

// cmake:sourcegroup=PathPlanner