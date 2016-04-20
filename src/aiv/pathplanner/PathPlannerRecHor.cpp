#include "aiv/pathplanner/PathPlannerRecHor.hpp"
#include "aiv/obstacle/Obstacle.hpp"
#include "aiv/robot/AIV.hpp"

#include "aiv/helpers/MyException.hpp"
//#include "aiv/pathplanner/constrJac.hpp"
//#include "aiv/pathplanner/eq_cons_jac.hpp"
#include <nlopt.h>
#include <omp.h>
#include <boost/foreach.hpp>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <string>

// #define foreach_ BOOST_FOREACH

// #ifndef HAVE_CONFIG_H
// #define HAVE_CONFIG_H
// #endif

using boost::property_tree::ptree;
using namespace Eigen;

namespace aiv
{

	PathPlannerRecHor::PathPlannerRecHor(std::string name, double updateTimeStep)
		: PathPlanner(name)
		, _updateTimeStep(updateTimeStep)
		, _updateCallCntr(0)
		, _lastStepMinDist(0.5)
		, _xTol(0.001)
		, _fTol(0.001)
		, _eqTol(0.001)
		, _ineqTol(0.001)
		, _lastMaxIteration(50)
		, _firstMaxIteration(50)
		, _interMaxIteration(50)

		, _conflictFreePathDeviation(4.0)
		, _interRobotSafetyDist(0.1)
		, _robotObstacleSafetyDist(0.1)

		, _poseOutput(PoseVector::Zero())
		, _velocityOutput(VeloVector::Zero())
		, _accelOutput(AccelVector::Zero())

		, _maxVelocity(VeloVector::Constant(std::numeric_limits<double>::infinity()))
		, _maxAcceleration(AccelVector::Constant(std::numeric_limits<double>::infinity()))

		, _planStage(INIT)
		//, _ongoingPlanIdx(-1)
		, _executingPlanIdx(-1)
		//, _isThereConfl(NONE)
		, _planLastPlan(false)
		, _optimizerType("SLSQP")
		// , _h(1e-15)
		, _comRange(15)
		, _firstPlanTimespan(1.0)
	{
		_planOngoingMutex.initialize();
		//this->opt_log.open("optlog///opt_log.txt");
		//_eq_log.open("optlog/eq_log.txt");
		read_xml("../../../../src/aiv/output.xml", _property_tree);
	}

	PathPlannerRecHor::~PathPlannerRecHor()
	{
		//this->opt_log.close();
		//_eq_log.close();
	}

	void PathPlannerRecHor::init(
		const PoseVector &initPose, // x, y, theta
		const VeloVector &initVelocity, // v, w
		const PoseVector &targetedPose, // x, y, theta
		const VeloVector &targetedVelocity, // v, w
		const VeloVector &maxVelocity, // v, w
		double compHorizon,
		double planHorizon,
		unsigned nTimeSamples,
		unsigned nIntervNonNull)
	{
		_initPose = initPose;
		_initFlat = FlatoutputMonocycle::poseToFlat(_initPose);
		_initVelocity = initVelocity;

		_targetedPose = targetedPose;
		_targetedFlat = FlatoutputMonocycle::poseToFlat(_targetedPose);
		_targetedVelocity = targetedVelocity;
		
		_poseOutput = initPose;
		_velocityOutput = initVelocity;

		_maxVelocity = maxVelocity;

		_compHorizon = compHorizon; // computation horizon
		_optPlanHorizon = planHorizon; // planning horizon
		_planHorizon = planHorizon; // planning horizon
		_refPlanHorizon = planHorizon;
		_nTimeSamples = nTimeSamples; // number of  time samples taken within a planning horizon
		
		unsigned nCtrlPts = nIntervNonNull + splDegree;
		_trajectory.setOption("nCtrlPts", nCtrlPts);
		_optTrajectory.setOption("nCtrlPts", nCtrlPts);
		_trajectory.setOption("nIntervNonNull", nIntervNonNull);
		_optTrajectory.setOption("nIntervNonNull", nIntervNonNull);
		
		_maxStraightDist = planHorizon * maxVelocity(FlatoutputMonocycle::linSpeedIdx);

		_latestFlat = _initFlat;
		_latestPose = _initPose;
		_initPoseForCurrPlan = initPose;
		_initPoseForFuturePlan = initPose;
		_latestVelocity = initVelocity;
	}

	void PathPlannerRecHor::init(
		const PoseVector &initPose, // x, y, theta
		const VeloVector &initVelocity, // v, w
		const PoseVector &targetedPose, // x, y, theta
		const VeloVector &targetedVelocity, // v, w
		const VeloVector &maxVelocity, // v, w
		const VeloVector &maxAcceleration, // dv, dw
		double compHorizon,
		double planHorizon,
		unsigned nTimeSamples,
		unsigned _nIntervNonNull)
	{
		init(initPose, initVelocity, targetedPose, targetedVelocity, maxVelocity, compHorizon, planHorizon, nTimeSamples, _nIntervNonNull);
		_maxAcceleration = maxAcceleration;
	}

	void PathPlannerRecHor::setOption(const std::string& optionName, const double optionValue)
	{
		if (optionName == "lastStepMinDist")
		{
			_lastStepMinDist = optionValue;
		}
		else if (optionName == "xTol")
		{
			_xTol = optionValue;
		}
		else if (optionName == "fTol")
		{
			_fTol = optionValue;
		}
		else if (optionName == "eqTol")
		{
			_eqTol = optionValue;
		}
		else if (optionName == "ineqTol")
		{
			_ineqTol = optionValue;
		}
		else if (optionName == "conflictFreePathDeviation")
		{
			_conflictFreePathDeviation = optionValue;
		}
		else if (optionName == "interRobotSafetyDist")
		{
			_interRobotSafetyDist = optionValue;
		}
		else if (optionName == "robotObstacleSafetyDist")
		{
			_robotObstacleSafetyDist = optionValue;
		}
		else if (optionName == "offsetTime")
		{
			_firstPlanTimespan = optionValue;
		}
	}
	void PathPlannerRecHor::setOption(const std::string& optionName, const bool optionValue)
	{
		if (optionName == "waitForThread")
		{
			_waitForThread = optionValue;
		}
	}
	void PathPlannerRecHor::setOption(const std::string& optionName, const unsigned optionValue)
	{
		if (optionName == "lastMaxIteration")
		{
			_lastMaxIteration = optionValue;
		}
		else if (optionName == "firstMaxIteration")
		{
			_firstMaxIteration = optionValue;
		}
		else if (optionName == "interMaxIteration")
		{
			_interMaxIteration = optionValue;
		}
	}
	void PathPlannerRecHor::setOption(const std::string& optionName, const std::string& optionValue)
	{
		if (optionName == "optimizerType")
		{
			_optimizerType = optionValue;
		}
	}

	// void PathPlannerRecHor::conflictEval(std::map<std::string, AIV *>
	// 		otherVehicles, const Displacementd & myRealPose)
	// {
	// 	// Clear conflicts robots maps
	// 	this->collisionAIVs.clear();
	// 	this->comOutAIVs.clear();

	// 	Vector2d curr2dPosition;
	// 	curr2dPosition << myRealPose.x(), myRealPose.y();

	// 	Vector2d otherRobPosition;

	// 	for (std::map<std::string, AIV *>::iterator it = otherVehicles.begin();
	// 			it != otherVehicles.end(); ++it)
	// 	{
	// 		otherRobPosition << it->second->getCurrentPosition().x(),
	// 				it->second->getCurrentPosition().y();

	// 		double dInterRobot = (curr2dPosition - otherRobPosition).norm();

	// 		double maxLinSpeed = std::max(_maxVelocity[
	// 				FlatoutputMonocycle::linSpeedIdx],
	// 				it->second->getPathPlanner()->getMaxLinVelocity());

	// 		double dSecutiry = _secRho +
	// 				it->second->getPathPlanner()->getSecRho();

	// 		if (dInterRobot <= dSecutiry + maxLinSpeed*_planHorizon)
	// 		{
	// 			this->collisionAIVs[it->first] = it->second;
	// 		}

	// 		double minComRange = std::min(this->comRange,
	// 				it->second->getPathPlanner()->getComRange());

	// 		if (this->comAIVsSet.find(it->first) != this->comAIVsSet.end() &&
	// 			dInterRobot + maxLinSpeed*_planHorizon >= minComRange)
	// 		{
	// 			this->comOutAIVs[it->first] = it->second;
	// 		}
	// 	}
	// }

	void PathPlannerRecHor::update(std::map<std::string, Obstacle *> detectedObst,
		std::map<std::string, AIV *> otherVehicles,
		const Displacementd & myRealPose) //const Displacementd & realPose, const Twistd &realVelocity)
	{
		// std::cout << "update" << std::endl;
		double currentPlanningTime = _updateTimeStep*_updateCallCntr;
		++_updateCallCntr;

		double evalTime = currentPlanningTime - (std::max(_executingPlanIdx, 0)*_compHorizon);

		// --- REAL COMPUTATION

		if (_planStage != DONE && _planStage != FINAL)
		{

			// First update call
			if (_updateCallCntr == 1)
			{
				_detectedObstacles = detectedObst;
				// std::cout << "conflictEval 1" << std::endl;
				//this->conflictEval(otherVehicles, myRealPose);
				// std::cout << "conflictEval 2" << std::endl;
				//this->initTimeOfCurrPlan = currentPlanningTime;
				_planOngoingMutex.lock();
				_planThread = boost::thread(&PathPlannerRecHor::_plan, this); // Do P0
				//std::cout << "_______ " << std::string(name).substr(0,10) <<" (C1) Spawn the first plan thread!!! ________ " << _executingPlanIdx << std::endl;
			}

			// If no plan is been executed yet but the fistPlanTimespan is over
			else if (_executingPlanIdx == -1 && currentPlanningTime >= _firstPlanTimespan) // Time so robot touch the floor
			{

				// P0 will begin to be executed next
				++_executingPlanIdx;

				// Reset update call counter so evaluation time be corret
				_updateCallCntr = 1; // the update for evalTime zero will be done next, during this same "update" call. Thus counter should be 1 for the next call.
				evalTime = 0.0;

				////std::cout << "firstPlanTimespan " <<  firstPlanTimespan << std::endl;
				if (!_planOngoingMutex.try_lock()) // We are supposed to get this lock
				{
					// "kill" planning thread putting something reasonable in the aux spline
					// OR pause the simulation for a while, waiting for the plan thread to finish (completly unreal) => planThread.join()
					//opt_log << "update: ####### HECK! Plan didn't finish before computing time expired #######" << std::endl;
					//std::cout << "update: ####### HECK! Plan didn't finish before computing time expired #######" << std::endl;
					if (_waitForThread == true)
					{
						_planThread.join();
					}
					else
					{
						_planThread.interrupt();
					}
					_planOngoingMutex.lock();
				}
				// Now we are sure that there is no ongoing planning!

				// update solution spline with the auxliar spline find in planning 0;
				// no need to lock a mutex associated to the auxiliar spline because we are sure there is no ongoing planning
				_trajectory = _optTrajectory;
				_initPoseForFuturePlan = _latestPose;
				
				// after the end of a planning thread we are to not be in the INIT stage
				// if at the end of a planning thread _planLastPlan is true the next plan to be executed shall be the last
				_planStage = _planLastPlan ? FINAL : INTER;

				if (_planStage == FINAL) // No more planning threads
				{
					_detectedObstacles = detectedObst; // maybe useless
					_planHorizon = _optPlanHorizon;
					_planOngoingMutex.unlock();
					//opt_log << "update: !!!!! Now goint to execute last plan !!!!!" << std::endl;
				}
				else
				{
					// plan next section!
					_detectedObstacles = detectedObst;
					//this->initTimeOfCurrPlan = currentPlanningTime;
					// std::cout << "conflictEval 1" << std::endl;
					//this->conflictEval(otherVehicles, myRealPose);
					// std::cout << "conflictEval 2" << std::endl;
					_planThread = boost::thread(&PathPlannerRecHor::_plan, this); // Do PX with X in (1, 2, ..., indefined_finit_value)
					//std::cout << "_______ " << std::string(name).substr(0,10) <<" (C2) Spawn the second plan thread!!! ________ " << _executingPlanIdx << std::endl;
					//opt_log << "update: _______ (C2) Spawn the second plan thread!!! ________ " << this->ongoingPlanIdx << std::endl;
				}
			}

			// If the robot started to execute the motion
			else if (_executingPlanIdx >= 0 && evalTime >= _compHorizon)
			{
				++_executingPlanIdx;

				evalTime -= _compHorizon; // "Fix" evalTime

				if (!_planOngoingMutex.try_lock()) // We are supposed to get this lock
				{
					//opt_log << "update: ####### HECK! Plan didn't finish before computing time expired #######" << std::endl;
					//std::cout << "update: ####### HECK! Plan didn't finish before computing time expired #######" << std::endl;
					// "kill" planning thread putting something reasonable in the aux spline
					// OR pause the simulation for a while, waiting for the plan thread to finish (completly unreal) => planThread.join()
					//xde::sys::Timer::Sleep( 0.02 );
					if (_waitForThread == true)
					{
						_planThread.join();
					}
					else
					{
						_planThread.interrupt();
					}
					_planOngoingMutex.lock();
				}
				// Now we are sure that there is no ongoing planning!

				// update solution spline with the auxliar spline;
				// no need to lock a mutex associated to the auxiliar spline because we are sure there is no ongoing planning

				_trajectory = _optTrajectory;
				_initPoseForCurrPlan = _initPoseForFuturePlan; // update base pose
				_initPoseForFuturePlan = _latestPose; // get latest position found by the plan that just ended (will be the next base pos)

				// if at the end of a planning thread _planLastPlan is true the next plan to be executed shall be the last
				_planStage = _planLastPlan ? FINAL : _planStage;

				if (_planStage == FINAL)
				{
					_planHorizon = _optPlanHorizon;
					_planOngoingMutex.unlock();
					//opt_log << "update: !!!!! Now goint to execute last plan !!!!!" << std::endl;
				}
				else
				{
					// plan next section!
					_detectedObstacles = detectedObst;
					//this->initTimeOfCurrPlan = currentPlanningTime;
					// std::cout << "conflictEval 1" << std::endl;
					//this->conflictEval(otherVehicles, myRealPose);
					// std::cout << "conflictEval 2" << std::endl;
					_planThread = boost::thread(&PathPlannerRecHor::_plan, this); // Do PX with X in (1, 2, ..., indefined_finit_value)
					//std::cout << "_______ " << std::string(name).substr(0,10) <<" (C3) Spawn new plan thread!!! ________ " << _executingPlanIdx << std::endl;
				}
			}
		}
		else if (_planStage == FINAL && evalTime > _planHorizon)
		{
			////std::cout << "gone to DONE" << std::endl;
			_planStage = DONE;
			_poseOutput = _targetedPose;
			_velocityOutput = _targetedVelocity;
			_accelOutput = AccelVector::Zero();
		}

		// --- APPARENT UPDATE

		if (_planStage == INIT)
		{
			// do nothing
			// double ctrlpts[20];
			// for (auto i = 0; i < 20; ++i)
			// 	ctrlpts[i] = -1;
			// _trajectory.getParameters(ctrlpts);
			// std::cout << "____________" << std::endl;
			// for (auto i = 0; i < 20; ++i)
			// 	std::cout << ctrlpts[i] << ", " << std::endl;
			// std::cout << "____________" << std::endl;
			//std::cout << _trajectory(evalTime*_planHorizon, 3) << std::endl;
		}
		else if (_planStage == DONE)
		{
			std::cout << "Planning is over!" << std::endl;
			std::cout << "Last step planning horizon: " << _planHorizon << std::endl;
			//boost::this_thread::sleep(boost::posix_time::milliseconds(300));
		}
		else //INTER or FINAL
		{
			// just use solSpline to get the nextReferences values
			//std::cout << "evalTime: " << evalTime << std::endl;

			NDerivativesMatrix derivFlat = _trajectory(evalTime, FlatoutputMonocycle::flatDerivDeg);

			Np1DerivativesMatrix derivFlat2 = _trajectory(evalTime, FlatoutputMonocycle::flatDerivDeg+1);

			_poseOutput = FlatoutputMonocycle::flatToPose(derivFlat);

			_poseOutput.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0) =
				_poseOutput.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0) +
				_initPoseForCurrPlan.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0);

			_velocityOutput = FlatoutputMonocycle::flatToVelocity(derivFlat);

			_accelOutput = FlatoutputMonocycle::flatToAcceleration(derivFlat2);
		}

		return;
	}

	void PathPlannerRecHor::_plan()
	{
		// GET ESTIMATE POSITION OF LAST POINT
		FlatVector remainingDistVectorUni = _targetedFlat - _latestFlat;
		double remainingDist = remainingDistVectorUni.norm();
		remainingDistVectorUni /= remainingDist;
		FlatVector lastPt = _maxStraightDist*remainingDistVectorUni;

		// RECIDING HORIZON STOP CONDITION 
		if (remainingDist < _lastStepMinDist + _compHorizon*_maxVelocity[FlatoutputMonocycle::linSpeedIdx])
		{
			//std::cout << "CONDITION FOR TERMINATION PLAN REACHED!" << std::endl;
			_planLastPlan = true;
			//estimate last planning horizon
			_optPlanHorizon = remainingDist / (_latestVelocity(FlatoutputMonocycle::linSpeedIdx,0) +  _targetedVelocity(FlatoutputMonocycle::linSpeedIdx,0))*2.;
			// TODO recompute n_ctrlpts n_knots
			// using _optTrajectory
		}
		else
			_optPlanHorizon = _refPlanHorizon;

		// GET WAYPOINT AND NEW DIRECTION

		FlatVector currDirec = (FlatVector() << cos(_latestPose(FlatoutputMonocycle::oriIdx, 0)), sin(_latestPose(FlatoutputMonocycle::oriIdx, 0))).finished();

		FlatVector newDirec;
		FlatVector wayPoint;
		//_findDirection(newDirec, wayPoint);

		_rotMat2WRef = (Matrix2d() << currDirec(0,0), -1.*currDirec(0,1), currDirec(0,1), currDirec(0,0)).finished();
		_rotMat2RRef = (Matrix2d() << currDirec(0,0), currDirec(0,1), -1.*currDirec(0,1), currDirec(0,0)).finished();
		// self._latest_rot2ref_mat = np.hstack((init_direc, np.multiply(np.flipud(init_direc), np.vstack((-1,1)))))
		// self._latest_rot2rob_mat = np.hstack((np.multiply(init_direc, np.vstack((1,-1))), np.flipud(init_direc)))
		
		FlatVector rotatedNewDirec = _rotMat2RRef*newDirec;

		double accel = _planLastPlan ? -1.*min(
			(_targetedVelocity(FlatoutputMonocycle::linSpeedIdx,0) + _latestVelocity(FlatoutputMonocycle::linSpeedIdx,0))/_optPlanHorizon,
			_maxAcceleration(FlatoutputMonocycle::linAccelIdx,0)) : _maxAcceleration(FlatoutputMonocycle::linAccelIdx,0);

		double maxDisplVariation = (_optPlanHorizon/(_optTrajectory.nParam() - 1))*_maxVelocity(FlatoutputMonocycle::linSpeedIdx,0);
		double prevDispl = 0.0;

		// Create a sampled trajectory for a "bounded uniformed accelerated motion" in x axis
		Matrix<double, FlatoutputMonocycle::flatDim, Dynamic> curveCurrDirec(FlatoutputMonocycle::flatDim, _optTrajectory.nParam());
		curveCurrDirec = Matrix<double, FlatoutputMonocycle::flatDim, Dynamic>::Zero(FlatoutputMonocycle::flatDim, _optTrajectory.nParam());
		
		// Create a sampled trajectory for a "bounded uniformed accelerated motion" in (direc-init_direc) direction in the xy plane
		Matrix<double, FlatoutputMonocycle::flatDim, Dynamic> curveNewDirec(FlatoutputMonocycle::flatDim, _optTrajectory.nParam());
		curveNewDirec = Matrix<double, FlatoutputMonocycle::flatDim, Dynamic>::Zero(FlatoutputMonocycle::flatDim, _optTrajectory.nParam());

		for (auto i=1; i<_optTrajectory.nParam(); ++i)
		{
			double deltaT = i*_optPlanHorizon/(_optTrajectory.nParam()-1);
			double displ = _latestVelocity(FlatoutputMonocycle::linSpeedIdx,0)*deltaT + accel/2.*deltaT*deltaT;
			displ = displ - prevDispl < maxDisplVariation ? max(displ, prevDispl) : prevDispl + maxDisplVariation;
			prevDispl = displ;
			curveCurrDirec(0,i) = displ;
			curveNewDirec.col(i) = displ*rotatedNewDirec;
		}

		Matrix<double, FlatoutputMonocycle::flatDim, Dynamic> curve(FlatoutputMonocycle::flatDim, _optTrajectory.nParam());
		//curveNewDirec = Matrix<FlatoutputMonocycle::flatDim, Dynamic>::Zero(FlatoutputMonocycle::flatDim, _optTrajectory.nParam());

		double magicNumber = 1.5; //FIXME no magic numbers, at least no hard coded
		double p;

		for (auto i=0; i < _optTrajectory.nParam(); ++i)
		//for (auto i=0; i < FlatoutputMonocycle::flatDim; ++i)
		{
			p = pow(double(i)/_optTrajectory.nParam(), magicNumber);
			curve.col(i) = curveCurrDirec.col(i)*(1.0-p) + curveNewDirec.col(i)*p;
		}

		// Feed aux trajectory with desired points and time horizon
		_optTrajectory.interpolate(curve, _optPlanHorizon);

		// CALL OPT SOLVER
		try
		{
			_solveOptPbl(); // after this call auxTrajectory has the optimized solution
		}
		catch (std::exception& e)
		{
			std::stringstream ss;
			ss << "PathPlannerRecHor::_plan: call to solve optimization problem failed. " << e.what();
			throw(MyException(ss.str()));
		}
		
		// UPDATES

		NDerivativesMatrix derivFlat = _optTrajectory(_compHorizon, FlatoutputMonocycle::flatDerivDeg);
		
		_latestFlat += derivFlat.col(0);

		PoseVector auxLatestPose = _latestPose;
		_latestPose = FlatoutputMonocycle::flatToPose(derivFlat);
		_latestPose.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0) +=
			auxLatestPose.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0);

		_latestVelocity = FlatoutputMonocycle::flatToVelocity(derivFlat);

		_planOngoingMutex.unlock();
	}

	void PathPlannerRecHor::_solveOptPbl()
	{

		unsigned nParam;
		unsigned nEq;
		unsigned nIneq;
		double(*objF) (unsigned, const double *, double *, void *);
		void(*eqF) (unsigned, double *, unsigned, const double*, double*, void*);
		void(*ieqF) (unsigned, double *, unsigned, const double*, double*, void*);

		double *optParam;
		double *tolEq;
		double *tolIneq;

		if (_planLastPlan == false)
		{

		// 	objF = PathPlannerRecHor::objectFunc;
		// 	eqF = PathPlannerRecHor::eqFunc;
		// 	ieqF = PathPlannerRecHor::ineqFunc;
			nParam = _optTrajectory.nParam()*FlatoutputMonocycle::flatDim;
			nEq = FlatoutputMonocycle::poseDim + FlatoutputMonocycle::veloDim;
			nIneq = FlatoutputMonocycle::veloDim * _nTimeSamples +
				FlatoutputMonocycle::accelDim * (_nTimeSamples + 1) +
				_nTimeSamples * _detectedObstacles.size();

			optParam = new double[nParam];
			_optTrajectory.getParameters(optParam);
			
			tolEq = new double[nEq];
			for (unsigned i = 0; i < nEq; ++i)
			{
				tolEq[i] = _eqTol;
			}

			tolIneq = new double[nIneq];
			for (unsigned i = 0; i < nIneq; ++i)
			{
				tolIneq[i] = _ineqTol;
			}
		}
		else
		{
		// 	objF = PathPlannerRecHor::objectFuncLS;
		// 	eqF = PathPlannerRecHor::eqFuncLS;
		// 	ieqF = PathPlannerRecHor::ineqFuncLS;
			nParam = _optTrajectory.nParam()*FlatoutputMonocycle::flatDim + 1;
			nEq = (FlatoutputMonocycle::poseDim + FlatoutputMonocycle::veloDim) * 2;
			nIneq = FlatoutputMonocycle::veloDim * (_nTimeSamples - 1) +
				FlatoutputMonocycle::accelDim * _nTimeSamples +
				(_nTimeSamples - 1) * _detectedObstacles.size();

			optParam = new double[nParam];
			optParam[0] = _optPlanHorizon;
			_optTrajectory.getParameters(&(optParam[1]));

			tolEq = new double[nEq];
			for (unsigned i = 0; i < nEq; ++i)
			{
				tolEq[i] = _eqTol;
			}

			tolIneq = new double[nIneq];
			for (unsigned i = 0; i < nIneq; ++i)
			{
				tolIneq[i] = _ineqTol;
			}
		}
		
		if (_optimizerType == "NONE")
		{
			// GET optParam from XML
			std::string base_name = "AdeptLynx";

			std::stringstream ss;
			ss << "root." << base_name << name[base_name.size()] << ".plan" << std::to_string(_ULONGLONG(_executingPlanIdx+1));

			std::string optParamString;

			try
			{
				optParamString = _property_tree.get<std::string>(ss.str());
			}
			catch (std::exception& e)
			{
				std::stringstream strs;
				strs << "PathPlannerRecHor::_solveOptPbl: property tree execption. Verify output.xml file and/or python plan generator.\n" << e.what();
				delete[] optParam;
				delete[] tolEq;
				delete[] tolIneq;
				throw(MyException(strs.str()));
			}

			//std::stringstream ss(optParamString);
			ss << optParamString;	
			std::string token;
			int i = 0;
			while (getline(ss, token, ','))
			{
				optParam[i++] = std::strtod(token.c_str(), NULL);
			}

			// objF(nParam, optParam, NULL, this);
			// double *constrEq = new double[nEq];
			// eqFunc(nEq, constrEq, nParam, optParam, NULL, this);
			// double *constrIneq = new double[nIneq];
			// ineqFunc(nIneq, constrIneq, nParam, optParam, NULL, this);

			// delete[] constrEq;
			// delete[] constrIneq;

		}
		else if (_optimizerType == "SLSQP" || _optimizerType == "COBYLA")
		{
			nlopt_opt opt;

			if (_optimizerType == "COBYLA")
			{
				opt = nlopt_create(NLOPT_LN_COBYLA, nParam);
			}
			else if (_optimizerType == "SLSQP")
			{
				opt = nlopt_create(NLOPT_LD_SLSQP, nParam);
			}
			else
			{
				std::stringstream ss;
				ss << "PathPlannerRecHor::_solveOptPbl: unknown optimization method [" << _optimizerType << "]. Verify config.xml.";
				delete[] optParam;
				delete[] tolEq;
				delete[] tolIneq;
				throw(MyException(ss.str()));
			}

			nlopt_set_xtol_rel(opt, _xTol);

			nlopt_set_min_objective(opt, objF, this);
			nlopt_add_equality_mconstraint(opt, nEq, eqF, this, tolEq);
			nlopt_add_inequality_mconstraint(opt, nIneq, ieqF, this, tolIneq);

			double minf;
			int status = nlopt_optimize(opt, optParam, &minf);

			// STATUS NUMBER MEANING
			//NLOPT_SUCCESS = 1
			//Generic success return value.
			//NLOPT_STOPVAL_REACHED = 2
			//Optimization stopped because stopval (above) was reached.
			//NLOPT_FTOL_REACHED = 3
			//Optimization stopped because ftol_rel or ftol_abs (above) was reached.
			//NLOPT_XTOL_REACHED = 4
			//Optimization stopped because xtol_rel or xtol_abs (above) was reached.
			//NLOPT_MAXEVAL_REACHED = 5
			//Optimization stopped because maxeval (above) was reached.
			//NLOPT_MAXTIME_REACHED = 6
			//Optimization stopped because maxtime (above) was reached.
			//[edit]
			//Error codes (negative return values)
			//NLOPT_FAILURE = -1
			//Generic failure code.
			//NLOPT_INVALID_ARGS = -2
			//Invalid arguments (e.g. lower bounds are bigger than upper bounds, an unknown algorithm was specified, etcetera).
			//NLOPT_OUT_OF_MEMORY = -3
			//Ran out of memory.
			//NLOPT_ROUNDOFF_LIMITED = -4
			//Halted because roundoff errors limited progress. (In this case, the optimization still typically returns a useful result.)
			//NLOPT_FORCED_STOP = -5
			//Halted because of a forced termination: the user called nlopt_force_stop(opt) on the optimizationís nlopt_opt object opt from the userís objective function or constraints.

		}
		else
		{
			std::stringstream ss;
			ss << "PathPlannerRecHor::_solveOptPbl: unknown optimization method [" << _optimizerType << "]. Verify config.xml.";
			delete[] optParam;
			delete[] tolEq;
			delete[] tolIneq;
			throw(MyException(ss.str()));
		}

		// std::ofstream arquivo;
		// arquivo.open("optlog/xiter.csv", std::fstream::app);
		// arquivo << "___________________________________________________" << std::endl;
		// arquivo.close();
		// arquivo.open("optlog/geqiter.csv", std::fstream::app);
		// arquivo << "___________________________________________________" << std::endl;
		// arquivo.close();
		// arquivo.open("optlog/gieqiter.csv", std::fstream::app);
		// arquivo << "___________________________________________________" << std::endl;
		// arquivo.close();
		// arquivo.open("optlog/citer.csv", std::fstream::app);
		// arquivo << "___________________________________________________" << std::endl;
		// arquivo.close();


		// Update trajectory with optimized parameters
		if (_planLastPlan == false)
		{
			if (_optimizerType == "NONE")
				_optTrajectory.updateFromUniform(optParam);
				//_optTrajectory.updateFromUniform(_rotMat2WRef*_optTrajectory.cArray2CtrlPtsMat(optParam));
			else
					_optTrajectory.update(_rotMat2WRef * _optTrajectory.cArray2CtrlPtsMat(optParam));
		}
		else
		{
			if (_optimizerType == "NONE")
				_optTrajectory.updateFromUniform(&(optParam[1]), optParam[0]);
				//_optTrajectory.updateFromUniform(_rotMat2WRef*_optTrajectory.cArray2CtrlPtsMat(&(optParam[1])), optParam[0]);
			else
				_optTrajectory.update(_rotMat2WRef * _optTrajectory.cArray2CtrlPtsMat(&(optParam[1])), optParam[0]);
			_optPlanHorizon = optParam[0];
		}

		delete[] optParam;
		delete[] tolEq;
		delete[] tolIneq;
		//boost::this_thread::sleep(boost::posix_time::milliseconds(200));
	}


	/*

	Inequations should be in the form:
	myconstraint(x) <= 0

	*/

	// double  PathPlannerRecHor::objectFunc(unsigned n, const double *x, double *grad, void *my_func_data)
	// {

	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(my_func_data);

	// 	// Create a CtrlPoints vector
	// 	MySpline::ControlPointVectorType ctrlPts(splDim, thisPP->noCtrlPts);

	// 	// Feed ctrlPts rows with values from the primal variables x
	// 	for (int i = 0; i < n; ++i)
	// 	{
	// 		ctrlPts(i % splDim, i / splDim) = x[i];
	// 	}

	// 	// Create a spline based on the new ctrlpts (using the same knots as before)
	// 	MySpline optSpline(thisPP->auxSpline.knots(), ctrlPts);

	// 	// Create a Matrix consisting of the flat output and its needed derivatives for the instant Tp (1.0)
	// 	NDerivativesMatrix derivFlat =
	// 		optSpline.derivatives(thisPP->finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg);

	// 	// Get pose at Tp from flatoutput
	// 	PoseVector poseAtTp = FlatoutputMonocycle::flatToPose(derivFlat);

	// 	// Compute euclidian distance to square from position at Tp and goal position which will be the cost
	// 	double fx = pow((poseAtTp.block(FlatoutputMonocycle::posIdx, 0, FlatoutputMonocycle::posDim, 1) -
	// 		(thisPP->targetedPose.block(FlatoutputMonocycle::posIdx, 0, FlatoutputMonocycle::posDim, 1) -
	// 			thisPP->latestPose.block(FlatoutputMonocycle::posIdx, 0, FlatoutputMonocycle::posDim, 1))).norm(), 2);

	// 	// KEEPING LOG
	// 	std::ofstream arquivo;
	// 	arquivo.open("optlog/xiter.csv", std::fstream::app);
	// 	for (int i = 0; i < int(n); ++i)
	// 	{
	// 		//x[i] = startSpline.ctrls()(i%(splDim-1)+1, i/(splDim-1));
	// 		arquivo << x[i] << ", ";

	// 	}
	// 	arquivo << std::endl;
	// 	arquivo.close();
	// 	arquivo.open("optlog/citer.csv", std::fstream::app);
	// 	arquivo << fx << std::endl;
	// 	arquivo.close();

	// 	// If grad not NULL the Jacobian matrix must be given
	// 	if (grad)
	// 	{
	// 		for (unsigned i = n - 1, j = 0; i > n - thisPP->splDim - 1; --i, ++j)
	// 		{
	// 			grad[i] = 2 * (x[i] -
	// 				(thisPP->targetedPose(i % thisPP->splDim + FlatoutputMonocycle::posIdx, 0) -
	// 					thisPP->latestPose(i % thisPP->splDim + FlatoutputMonocycle::posIdx, 0)));
	// 		}
	// 		for (unsigned i = 0; i < n - thisPP->splDim; ++i)
	// 		{
	// 			grad[i] = 0.0;
	// 		}
	// 	}
	// 	return fx;
	// }

	// void PathPlannerRecHor::eval_eq(double *result, unsigned n, const double* x, void* data)
	// {
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	// Create a CtrlPoints vector
	// 	MySpline::ControlPointVectorType ctrlPts(splDim, thisPP->noCtrlPts);

	// 	// Feed remaining rows with values from the primal variables x
	// 	for (int i = 0; i < n; ++i)
	// 	{
	// 		ctrlPts(i % splDim, i / splDim) = x[i];
	// 	}


	// 	// Create a spline based on the new ctrlpts (using the same knots as before)
	// 	MySpline optSpline(thisPP->auxSpline.knots(), ctrlPts);


	// 	// EQUATIONS


	// 	// Create a Matrix consisting of the flat output and its needed derivatives for the instant T0 (0.0)
	// 	NDerivativesMatrix derivFlatEq =
	// 		optSpline.derivatives(0.0, FlatoutputMonocycle::flatDerivDeg);

	// 	// Get error from pose at T0 and pose at the end of the previous plan (or initial position if this is the very first plan)
	// 	//PoseVector poseAtT0 = FlatoutputMonocycle::flatToPose(derivFlatEq);
	// 	PoseVector diffPoseAtT0 = FlatoutputMonocycle::flatToPose(derivFlatEq);
	// 	diffPoseAtT0.tail(1) -= thisPP->latestPose.tail(1);
	// 	/*diffPoseAtT0.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0) =
	// 		poseAtT0.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0);*/

	// 	VeloVector diffVelocityAtT0 = FlatoutputMonocycle::flatToVelocity(derivFlatEq) - thisPP->latestVelocity;

	// 	int i = 0;
	// 	for (; i < FlatoutputMonocycle::poseDim; ++i)
	// 	{
	// 		result[i] = diffPoseAtT0[i];
	// 		//std::cout << "G[" << i << "] " << g[i] << std::endl;
	// 	}
	// 	for (int j = i; j - i < FlatoutputMonocycle::veloDim; ++j)
	// 	{
	// 		result[j] = diffVelocityAtT0[j - i];
	// 		//std::cout << "G[" << j << "] " << g[j] << std::endl;
	// 	}
	// }

	// void PathPlannerRecHor::eqFunc(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data)
	// {
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	thisPP->eval_eq(result, n, x, data);



	// 	if (grad)
	// 	{
	// 		/* ALGEBRIC JACOBIAN */

	// 		int span = thisPP->findspan(0.0);
	// 		span -= _nIntervNonNull-1;

	// 		qJacMatrix qJ = qJac(0.0, thisPP->finalPlanHorizon, span, x, n, thisPP->splDegree);
	// 		dqJacMatrix dqJ = dqJac(0.0, thisPP->finalPlanHorizon, span, x, n, thisPP->splDegree);

	// 		MatrixXd J(qJ.rows()+dqJ.rows(), qJ.cols());
	// 		J << qJ, dqJ;

	// 		Map<MatrixXd>(grad, J.cols(), J.rows()) =
	// 				J.transpose();
	// 	}
	// }

	// void PathPlannerRecHor::eval_ineq(double *result, unsigned n, const double* x, void* data)
	// {
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	// Create a CtrlPoints vector
	// 	MySpline::ControlPointVectorType ctrlPts(splDim, thisPP->noCtrlPts);

	// 	// Feed remaining rows with values from the primal variables x
	// 	for (int i = 0; i < n; ++i)
	// 	{
	// 		ctrlPts(i % splDim, i / splDim) = x[i];
	// 	}

	// 	// Create a spline based on the new ctrlpts (using the same knots as before)
	// 	MySpline optSpline(thisPP->auxSpline.knots(), ctrlPts);


	// 	// INEQUATIONS


	// 	Np1DerivativesMatrix derivFlat;
	// 	NDerivativesMatrix derivFlatSmall;

	// 	// Create Matrices for storing velocity and acceleration
	// 	VeloVector velocity;
	// 	AccelVector acceleration;

	// 	PoseVector pose;

	// 	derivFlat =
	// 		optSpline.derivatives(0.0, FlatoutputMonocycle::flatDerivDeg + 1);

	// 	////std::cout << "got acc" << std::endl;
	// 	acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

	// 	int j = 0;
	// 	for (; j < FlatoutputMonocycle::accelDim; ++j)
	// 	{
	// 		const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
	// 		result[j] = absAcc - thisPP->maxAcceleration(j, 0);
	// 		////std::cout << "G[" << nEq+j+(i-1)*ieqPerSample << "] " << g[nEq+j+(i-1)*ieqPerSample] << std::endl;
	// 	}

	// 	int nAcc = j;
	// 	int ieqPerSample = (FlatoutputMonocycle::veloDim + FlatoutputMonocycle::accelDim + thisPP->detectedObstacles.size());

	// 	//#pragma omp parallel for
	// 	for (int i = 1; i <= int(thisPP->_nTimeSamples); ++i)
	// 	{
	// 		////std::cout << "IEQ: SAMPLE IDX: " << i << std::endl;
	// 		derivFlat =
	// 			optSpline.derivatives(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg + 1);

	// 		////std::cout << "got derive meatrix" << std::endl;
	// 		// reusing derivFlatEq variable, ugly but let's move on
	// 		derivFlatSmall = derivFlat.block<FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 1>(0, 0);

	// 		////std::cout << "got vel" << std::endl;
	// 		velocity = FlatoutputMonocycle::flatToVelocity(derivFlatSmall);

	// 		pose = FlatoutputMonocycle::flatToPose(derivFlatSmall);

	// 		////std::cout << "got acc" << std::endl;
	// 		acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

	// 		//int ieqPerSample = (FlatoutputMonocycle::veloDim);

	// 		int j;
	// 		for (j = 0; j < FlatoutputMonocycle::accelDim; ++j)
	// 		{
	// 			const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
	// 			result[nAcc + j + (i - 1)*ieqPerSample] = absAcc - thisPP->maxAcceleration(j, 0);
	// 			////std::cout << "G[" << nEq+j+(i-1)*ieqPerSample << "] " << g[nEq+j+(i-1)*ieqPerSample] << std::endl;
	// 		}

	// 		int k;
	// 		for (k = j; k - j < FlatoutputMonocycle::veloDim; ++k)
	// 		{
	// 			const double absVel = (velocity(k - j, 0) > -velocity(k - j, 0)) ? velocity(k - j, 0) : -velocity(k - j, 0);
	// 			result[nAcc + k + (i - 1)*ieqPerSample] = absVel - thisPP->maxVelocity(k - j, 0);
	// 			////std::cout << "G[" << nEq+k+(i-1)*ieqPerSample << "] " << g[nEq+k+(i-1)*ieqPerSample] << std::endl;
	// 		}

	// 		// Obstacles
	// 		std::map<std::string, Obstacle *>::iterator it;
	// 		for (it = thisPP->detectedObstacles.begin(), j = k; j - k < thisPP->detectedObstacles.size(); ++j, ++it)
	// 		{
	// 			result[nAcc + (i - 1)*ieqPerSample + j] = -1. * it->second->distToAIV(pose.head(2) + thisPP->latestPose.head(2), thisPP->secRho);
	// 		}
	// 	}
	// }

	// void PathPlannerRecHor::eval_ineq_coll(double *result, unsigned n, const double* x, void* data)
	// {
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	// Create a CtrlPoints vector
	// 	MySpline::ControlPointVectorType ctrlPts(splDim, thisPP->noCtrlPts);

	// 	// Feed remaining rows with values from the primal variables x
	// 	for (int i = 0; i < n; ++i)
	// 	{
	// 		ctrlPts(i % splDim, i / splDim) = x[i];
	// 	}

	// 	// Create a spline based on the new ctrlpts (using the same knots as before)
	// 	MySpline optSpline(thisPP->auxSpline.knots(), ctrlPts);


	// 	// INEQUATIONS


	// 	Np1DerivativesMatrix derivFlat;
	// 	NDerivativesMatrix derivFlatSmall;

	// 	// Create Matrices for storing velocity and acceleration
	// 	VeloVector velocity;
	// 	AccelVector acceleration;

	// 	PoseVector pose;

	// 	derivFlat =
	// 		optSpline.derivatives(0.0, FlatoutputMonocycle::flatDerivDeg + 1);

	// 	////std::cout << "got acc" << std::endl;
	// 	acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

	// 	int j = 0;
	// 	for (; j < FlatoutputMonocycle::accelDim; ++j)
	// 	{
	// 		const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
	// 		result[j] = absAcc - thisPP->maxAcceleration(j, 0);
	// 		//std::cout << "G[" << j << "] " << result[j] << std::endl;
	// 	}

	// 	int nAcc = j;
	// 	int ieqPerSample = (FlatoutputMonocycle::veloDim + FlatoutputMonocycle::accelDim + thisPP->detectedObstacles.size());

	// 	//#pragma omp parallel for
	// 	for (int i = 1; i <= int(thisPP->_nTimeSamples); ++i)
	// 	{
	// 		//////std::cout << "IEQ: SAMPLE IDX: " << i << std::endl;
	// 		derivFlat =
	// 			optSpline.derivatives(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg + 1);

	// 		// reusing derivFlatEq variable, ugly but let's move on
	// 		derivFlatSmall = derivFlat.block<FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 1>(0, 0);

	// 		velocity = FlatoutputMonocycle::flatToVelocity(derivFlatSmall);

	// 		pose = FlatoutputMonocycle::flatToPose(derivFlatSmall);

	// 		acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

	// 		//int ieqPerSample = (FlatoutputMonocycle::veloDim);

	// 		int j;
	// 		for (j = 0; j < FlatoutputMonocycle::accelDim; ++j)
	// 		{
	// 			const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
	// 			result[nAcc + j + (i - 1)*ieqPerSample] = absAcc - thisPP->maxAcceleration(j, 0);
	// 			//std::cout << "G[" << nAcc + j + (i - 1)*ieqPerSample << "] " << result[nAcc + j + (i - 1)*ieqPerSample] << std::endl;
	// 		}

	// 		int k;
	// 		for (k = j; k - j < FlatoutputMonocycle::veloDim; ++k)
	// 		{
	// 			const double absVel = (velocity(k - j, 0) > -velocity(k - j, 0)) ? velocity(k - j, 0) : -velocity(k - j, 0);
	// 			result[nAcc + k + (i - 1)*ieqPerSample] = absVel - thisPP->maxVelocity(k - j, 0);
	// 			//std::cout << "G[" << nAcc + k + (i - 1)*ieqPerSample << "] " << result[nAcc + k + (i - 1)*ieqPerSample] << std::endl;
	// 		}

	// 		// Obstacles
	// 		std::map<std::string, Obstacle *>::iterator it;
	// 		for (it = thisPP->detectedObstacles.begin(), j = k; j - k < thisPP->detectedObstacles.size(); ++j, ++it)
	// 		{
	// 			result[nAcc + (i - 1)*ieqPerSample + j] = -1. * it->second->distToAIV(pose.head(2) + thisPP->latestPose.head(2), thisPP->secRho);
	// 			//std::cout << "G[" << nAcc + (i - 1)*ieqPerSample + j << "] " << result[nAcc + (i - 1)*ieqPerSample + j] << std::endl;

	// 		}

	// 	}

	// 	NDerivativesMatrix derivFlatSmallOther;
	// 	PoseVector myPose;
	// 	PoseVector otherPose;


	// 	for (std::map<std::string, AIV* >::const_iterator
	// 			it = thisPP->collisionAIVs.begin();
	// 			it != thisPP->collisionAIVs.end();
	// 			++it)
	// 	{
	// 		int i, j, k;
	// 		for (i = thisPP->conflictInfo[it->first][0]+1, j = thisPP->conflictInfo[it->first][1]+1, k=0; i <= int(thisPP->_nTimeSamples) && j <= int(thisPP->_nTimeSamples); ++i, ++j, ++k)
	// 		{
	// 			derivFlatSmall =
	// 				optSpline.derivatives(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg);

	// 			derivFlatSmallOther = 
	// 				it->second->getPathPlanner()->getSpline().derivatives(double(j) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg);

	// 			myPose = FlatoutputMonocycle::flatToPose(derivFlatSmall);
	// 			otherPose = FlatoutputMonocycle::flatToPose(derivFlatSmallOther);

	// 			result[nAcc + int(thisPP->_nTimeSamples)*ieqPerSample + k] =
	// 				thisPP->secRho + it->second->getPathPlanner()->getSecRho() - (myPose.head(2) - otherPose.head(2)).norm();
	// 			//std::cout << "dist: " << thisPP->secRho + it->second->getPathPlanner()->getSecRho() << "myPos: " << myPose.head(2) << "othPos: " << otherPose.head(2) << std::endl;
	// 			// result[nAcc + int(thisPP->_nTimeSamples)*ieqPerSample + k] =
	// 				// -1;
	// 			// std::cout << "G[" << nAcc + int(thisPP->_nTimeSamples)*ieqPerSample + k << "] " << result[nAcc + int(thisPP->_nTimeSamples)*ieqPerSample + k] << std::endl;

	// 		}
	// 	}
	// }

	// void PathPlannerRecHor::eval_dobst_ineq(double *result, unsigned n, const double* x, void* data)
	// {
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	// Create a CtrlPoints vector
	// 	MySpline::ControlPointVectorType ctrlPts(splDim, thisPP->noCtrlPts);

	// 	// Feed remaining rows with values from the primal variables x
	// 	for (int i = 0; i < n; ++i)
	// 	{
	// 		ctrlPts(i % splDim, i / splDim) = x[i];
	// 	}

	// 	// Create a spline based on the new ctrlpts (using the same knots as before)
	// 	MySpline optSpline(thisPP->auxSpline.knots(), ctrlPts);


	// 	// Dist 2 obst INEQUATIONS

	// 	NDerivativesMatrix derivFlatSmall;

	// 	PoseVector pose;

	// 	//#pragma omp parallel for
	// 	for (int i = 1; i <= int(thisPP->_nTimeSamples); ++i)
	// 	{
	// 		derivFlatSmall = optSpline.derivatives(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg);

	// 		pose = FlatoutputMonocycle::flatToPose(derivFlatSmall);

	// 		// Obstacles
	// 		std::map<std::string, Obstacle *>::iterator it;
	// 		int j;
	// 		for (it = thisPP->detectedObstacles.begin(), j = 0; j < thisPP->detectedObstacles.size(); ++j, ++it)
	// 		{
	// 			result[(i - 1)*thisPP->detectedObstacles.size() + j] = -1. * it->second->distToAIV(pose.head(2) + thisPP->latestPose.head(2), 0.55);
	// 		}
	// 	}
	// }


	// void PathPlannerRecHor::ineqFunc(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data)
	// {
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	thisPP->eval_ineq(result, n, x, data);

	// 	std::ofstream arquivo;
	// 	arquivo.open("optlog/gieqiter.csv", std::fstream::app);
	// 	for (int i = 0; i < int(m); ++i)
	// 	{
	// 		//x[i] = startSpline.ctrls()(i%(splDim-1)+1, i/(splDim-1));
	// 		arquivo << result[i] << ", ";

	// 	}
	// 	arquivo << std::endl;
	// 	arquivo.close();

	// 	const int accDim = FlatoutputMonocycle::accelDim;
	// 	const int velDim = FlatoutputMonocycle::veloDim;
	// 	//int Xdim = thisPP->noCtrlPts * FlatoutputMonocycle::flatDim;
	// 	int nobst = thisPP->detectedObstacles.size();

	// 	if (grad)
	// 	{
	// 		int span = thisPP->findspan(0.0);
	// 		span -= _nIntervNonNull - 1;

	// 		////std::cout << "ineq grad\n";
	// 		MatrixXd J(m, n);

	// 		J.block(0, 0, accDim, n) = absddqJac(0.0, thisPP->finalPlanHorizon, span, x, n, thisPP->splDegree);

	// 		for (int i = 1; i <= int(thisPP->_nTimeSamples); ++i)
	// 		{

	// 			span = thisPP->findspan(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon) - (_nIntervNonNull - 1);

	// 			J.block(accDim + (i - 1)*(accDim + velDim + nobst), 0, accDim, n) =
	// 				absddqJac(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, thisPP->finalPlanHorizon, span, x, n, thisPP->splDegree);

	// 			J.block(accDim * 2 + (i - 1)*(accDim + velDim + nobst), 0, velDim, n) =
	// 				absdqJac(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, thisPP->finalPlanHorizon, span, x, n, thisPP->splDegree);

	// 			int j;
	// 			std::map<std::string, Obstacle *>::iterator it;
	// 			for (it = thisPP->detectedObstacles.begin(), j = 0; it != thisPP->detectedObstacles.end(); ++it, ++j)
	// 			{

	// 				/*J.block(j + accDim * 2 + velDim + (i - 1)*(accDim + velDim + nobst), 0, 1, n) =
	// 					obstDistJac(double(i) / thisPP->_nTimeSamples * thisPP->finalPlanHorizon, thisPP->finalPlanHorizon, span, x, n, thisPP->splDegree, it->second->getPosition().block(0, 0, 2, 1));*/
	// 			}
	// 		}

	// 		Map<MatrixXd>(grad, J.cols(), J.rows()) = J.transpose();

	// 		const double eps = sqrt(std::numeric_limits< double >::epsilon());

	// 		double *constrPre = new double[thisPP->_nTimeSamples*thisPP->detectedObstacles.size()];
	// 		double *constrPos = new double[thisPP->_nTimeSamples*thisPP->detectedObstacles.size()];
	// 		double *x1 = new double[n];
	// 		double h, dx;

	// 		const int velaccDim = FlatoutputMonocycle::accelDim + FlatoutputMonocycle::veloDim;
	// 		const int vel2accDim = 2 * FlatoutputMonocycle::accelDim + FlatoutputMonocycle::veloDim;

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			x1[i] = x[i];
	// 		}

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			h = x[i] < eps*thisPP->h ? eps*thisPP->h : eps*x[i];
	// 			//h = x[i] < eps ? eps*eps : eps*x[i];
	// 			//h = eps;
	// 			x1[i] = x[i] + h;
	// 			thisPP->eval_dobst_ineq(constrPos, n, x1, data);
	// 			dx = x1[i] - x[i];
	// 			x1[i] = x[i] - h;
	// 			thisPP->eval_dobst_ineq(constrPre, n, x1, data);
	// 			dx = dx + x[i] - x1[i];
	// 			//dx = 2 * h;
	// 			x1[i] = x[i];
	// 			////std::cout << "INEQ: " << x[i] << ", " << eps << ", " << h << ", " << dx << std::endl;
	// 			for (int j = 0; j < thisPP->detectedObstacles.size(); ++j)
	// 			{
	// 				for (int k = 0; k < thisPP->_nTimeSamples; ++k)
	// 				{
	// 					////std::cout << j * (velaccDim + thisPP->detectedObstacles.size()) + vel2accDim << ", " << i << std::endl;
	// 					grad[(k * (velaccDim + thisPP->detectedObstacles.size()) + vel2accDim + j) * n + i] = 
	// 							(constrPos[k*thisPP->detectedObstacles.size()+j] - constrPre[k*thisPP->detectedObstacles.size()+j]) / dx;
	// 					////std::cout << grad[j*n + i] << std::endl;
	// 				}
	// 			}
				
	// 		}
	// 		delete[] constrPre;
	// 		delete[] constrPos;
	// 		delete[] x1;

	// 		//MatrixXd e = Map<MatrixXd>(grad, m, n);

	// 		/*//std::cout << n << ", " << m << std::endl;

	// 		for (int i = 0; i < m; ++i)
	// 		{
	// 			for (int j = 0; j < n; ++j)
	// 			{
	// 				//std::cout << grad[i*n + j] << ", ";
	// 			}
	// 			//std::cout << std::endl;
	// 		}

	// 		//std::cout << J << std::endl;*/

	// 		//for (int i = 0; i < thisPP->_nTimeSamples; ++i)
	// 		//{
	// 		//	for (int j = 0; j < n; ++j)
	// 		//	{
	// 		//		////std::cout << i * 6 + 6 << ", " << j << std::endl;
	// 		//		J(i*6 + 6, j) = grad[(i * 6 + 6)*n + j];
	// 		//		J(i*6 + 6 + 1, j) = grad[(i * 6 + 6 + 1)*n + j];
	// 		//	}
	// 		//}
	// 		/*for (int i = 0; i < m; ++i)
	// 		{
	// 			for (int j = 0; j < n; ++j)
	// 			{
	// 				J(i, j) = grad[(i)*n + j];
	// 			}
	// 		}*/

	// 		////std::cout << "\nGrad\n" << Map<MatrixXd>(grad, 1, m*n) << std::endl;
	// 		//IOFormat CleanFmt(3, 0, " ", "\n", "", "");
	// 		////std::cout << "\nJ\n" << J.block<70, 8>(2, 0).format(CleanFmt) << std::endl;

	// 		/*MatrixXd e(m, n);

	// 		for (int i = 0; i < m; ++i)
	// 		{
	// 			for (int j = 0; j < n; ++j)
	// 			{
	// 				e(i, j) = grad[(i)*n + j];
	// 			}
	// 		}*/
	// 		//MatrixXd e = Map<MatrixXd>(grad, n, m);
	// 		////std::cout << "\nJnum\n" << (J.block<86, 14>(0, 0) - e.block<86,14>(0, 0)).format(CleanFmt) << std::endl

	// 		//boost::this_thread::sleep(boost::posix_time::milliseconds(2000));
	// 	}
	// }
	// void PathPlannerRecHor::ineqFuncColl(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data)
	// {
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	thisPP->eval_ineq_coll(result, n, x, data);

	// 	if (grad)
	// 	{
	// 		const double eps = sqrt(std::numeric_limits< double >::epsilon());

	// 		double *constrPre = new double[m];
	// 		double *constrPos = new double[m];
	// 		double *x1 = new double[n];
	// 		double h, dx;

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			x1[i] = x[i];
	// 		}

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			h = x[i] < eps*thisPP->h ? eps*thisPP->h : eps*x[i];
	// 			x1[i] = x[i] + h;
	// 			thisPP->eval_ineq_coll(constrPos, n, x1, data);
	// 			dx = x1[i] - x[i];
	// 			x1[i] = x[i] - h;
	// 			thisPP->eval_ineq_coll(constrPre, n, x1, data);
	// 			dx = dx + x[i] - x1[i];
	// 			x1[i] = x[i];

	// 			for (int j = 0; j < m; ++j)
	// 			{
	// 				grad[j*n + i] = (constrPos[j] - constrPre[j]) / dx;
	// 			}
				
	// 		}
	// 		delete[] constrPre;
	// 		delete[] constrPos;
	// 		delete[] x1;
	// 	}
	// }

	// // Stand alone constraints, final step

	// double  PathPlannerRecHor::objectFuncLS(unsigned n, const double *x, double *grad, void *my_func_data)
	// {
	// 	double fx = x[0] * x[0];

	// 	////std::cout << "objectFunLS" << std::endl;

	// 	if (grad)
	// 	{
	// 		//myNLP->eval_grad_f ( n, x, true, grad );
	// 		grad[0] = 2 * x[0];
	// 		for (unsigned i = 1; i < n; ++i)
	// 		{
	// 			grad[i] = 0.0;
	// 		}
	// 	}

	// 	return fx;
	// }

	// void PathPlannerRecHor::eval_eqLS(double *result, unsigned n, const double* x, void* data)
	// {
	// 	double finalPlanHorizon = x[0];

	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	// Create a CtrlPoints vector
	// 	MySpline::ControlPointVectorType ctrlPts(splDim, thisPP->noCtrlPts);

	// 	// Feed remaining rows with values from the primal variables x
	// 	for (int i = 1; i < n; ++i)
	// 	{
	// 		ctrlPts((i - 1) % splDim, (i - 1) / splDim) = x[i];
	// 	}

	// 	// Create a spline based on the new ctrlpts (using the same knots as before)
	// 	MySpline optSpline(thisPP->genKnots(0.0, finalPlanHorizon, false), ctrlPts); // has to be false, otherwise jacobien will be wrong


	// 	// EQUATIONS


	// 	// @t=0

	// 	// Create a Matrix consisting of the flat output and its needed derivatives for the instant T0 (0.0)
	// 	NDerivativesMatrix derivFlatEq =
	// 		optSpline.derivatives(0.0, FlatoutputMonocycle::flatDerivDeg);

	// 	// Get error from pose at T0 and pose at the end of the previous plan (or initial position if this is the very first plan)
	// 	PoseVector diffPose = FlatoutputMonocycle::flatToPose(derivFlatEq);
	// 	diffPose.tail(1) -= thisPP->latestPose.tail(1);

	// 	VeloVector diffVelocity = FlatoutputMonocycle::flatToVelocity(derivFlatEq) - thisPP->latestVelocity;

	// 	int i = 0;
	// 	for (; i < FlatoutputMonocycle::poseDim; ++i)
	// 	{
	// 		result[i] = diffPose[i];
	// 	}
	// 	int j = i;
	// 	for (; j - i < FlatoutputMonocycle::veloDim; ++j)
	// 	{
	// 		result[j] = diffVelocity[j - i];
	// 	}

	// 	// @t=Tp

	// 	derivFlatEq =
	// 		optSpline.derivatives(finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg);

	// 	// Get error from pose at Tp and targeted pose
	// 	diffPose = FlatoutputMonocycle::flatToPose(derivFlatEq);
	// 	diffPose.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0) +=
	// 		thisPP->latestPose.block<FlatoutputMonocycle::posDim, 1>(FlatoutputMonocycle::posIdx, 0);
	// 	diffPose -= thisPP->targetedPose;

	// 	diffVelocity = FlatoutputMonocycle::flatToVelocity(derivFlatEq) - thisPP->targetedVelocity;

	// 	for (i = j; i - j < FlatoutputMonocycle::poseDim; ++i)
	// 	{
	// 		result[i] = diffPose[i - j];
	// 	}
	// 	for (j = i; j - i < FlatoutputMonocycle::veloDim; ++j)
	// 	{
	// 		result[j] = diffVelocity[j - i];
	// 	}
	// }

	// void PathPlannerRecHor::eqFuncLS(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data)
	// {
	// 	////std::cout << "eqFuncLS" << std::endl;
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	thisPP->eval_eqLS(result, n, x, data);

	// 	if (grad)
	// 	{
	// 		const double eps = sqrt(std::numeric_limits< double >::epsilon());
	// 		double *constrPre = new double[m];
	// 		double *constrPos = new double[m];
	// 		double *x1 = new double[n];
	// 		double h, dx;

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			x1[i] = x[i];
	// 		}

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			//h = x[i] < eps*1e2 ? eps*eps*1e2 : eps*x[i];
	// 			h = x[i] < eps ? eps*eps : eps*x[i];
	// 			//h = eps;
	// 			x1[i] = x[i] + h;
	// 			thisPP->eval_eqLS(constrPos, n, x1, data);
	// 			dx = x1[i] - x[i];
	// 			x1[i] = x[i] - h;
	// 			thisPP->eval_eqLS(constrPre, n, x1, data);
	// 			dx = dx + x[i] - x1[i];
	// 			//dx = 2 * h;
	// 			x1[i] = x[i];
	// 			////std::cout << "INEQ: " << x[i] << ", " << eps << ", " << h << ", " << dx << std::endl;
	// 			for (int j = 0; j < m; ++j)
	// 			{
	// 				grad[j*n + i] = (constrPos[j] - constrPre[j]) / dx;
	// 				////std::cout << grad[j*n + i] << std::endl;
	// 			}
	// 		}
	// 		delete[] constrPre;
	// 		delete[] constrPos;
	// 		delete[] x1;
	// 	}
	// }

	// void PathPlannerRecHor::eval_ineqLS(double *result, unsigned n, const double* x, void* data)
	// {
	// 	////std::cout << "eval_ineqLS" << std::endl;
	// 	double finalPlanHorizon = x[0];

	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	// Create a CtrlPoints vector
	// 	MySpline::ControlPointVectorType ctrlPts(splDim, thisPP->noCtrlPts);

	// 	// Feed remaining rows with values from the primal variables x
	// 	for (int i = 1; i < n; ++i)
	// 	{
	// 		ctrlPts((i - 1) % splDim, (i - 1) / splDim) = x[i];
	// 	}

	// 	// Create a spline based on the new ctrlpts (using the same knots as before)
	// 	MySpline optSpline(thisPP->genKnots(0.0, finalPlanHorizon, false), ctrlPts); // has to be false, otherwise jacobien will be wrong

	// 	////std::cout << "eval_ineqLS 2" << std::endl;
	// 	// INEQUATIONS


	// 	typedef Matrix< double, FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 2 > Np1DerivativesMatrix;

	// 	Np1DerivativesMatrix derivFlat;
	// 	NDerivativesMatrix derivFlatSmall;

	// 	// inequations t = 0.0 and t = Tp

	// 	// Create Matrices for storing velocity and acceleration
	// 	VeloVector velocity;
	// 	AccelVector acceleration;

	// 	derivFlat =
	// 		optSpline.derivatives(0.0, FlatoutputMonocycle::flatDerivDeg + 1);

	// 	acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

	// 	int j = 0;
	// 	for (; j < FlatoutputMonocycle::accelDim; ++j)
	// 	{
	// 		const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
	// 		result[j] = absAcc - thisPP->maxAcceleration(j, 0);
	// 		////std::cout << "G[" << j << "] " << result[j] << std::endl;
	// 	}

	// 	derivFlat =
	// 		optSpline.derivatives(finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg + 1);

	// 	acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

	// 	int nAcc = j;

	// 	for (j = nAcc; j - nAcc < FlatoutputMonocycle::accelDim; ++j)
	// 	{
	// 		const double absAcc = (acceleration(j - nAcc, 0) > -acceleration(j - nAcc, 0)) ? acceleration(j - nAcc, 0) : -acceleration(j - nAcc, 0);
	// 		result[j] = absAcc - thisPP->maxAcceleration(j - nAcc, 0);
	// 		////std::cout << "G[" << j << "] " << result[j] << std::endl;
	// 	}
	// 	nAcc = 2*FlatoutputMonocycle::accelDim;

	// 	// inequations in ]0.0, Tp[

	// 	//#pragma omp parallel for
	// 	for (int i = 1; i < int(thisPP->_nTimeSamples); ++i)
	// 	{
	// 		////std::cout << "IEQ: SAMPLE IDX: " << i << std::endl;
	// 		derivFlat =
	// 			optSpline.derivatives(double(i) / thisPP->_nTimeSamples * finalPlanHorizon, FlatoutputMonocycle::flatDerivDeg + 1);

	// 		////std::cout << "got derive meatrix" << std::endl;
	// 		// reusing derivFlatEq variable, ugly but let's move on
	// 		derivFlatSmall = derivFlat.block<FlatoutputMonocycle::flatDim, FlatoutputMonocycle::flatDerivDeg + 1>(0, 0);

	// 		////std::cout << "got vel" << std::endl;
	// 		velocity = FlatoutputMonocycle::flatToVelocity(derivFlatSmall);

	// 		////std::cout << "got acc" << std::endl;
	// 		acceleration = FlatoutputMonocycle::flatToAcceleration(derivFlat);

	// 		int ieqPerSample = (FlatoutputMonocycle::veloDim + FlatoutputMonocycle::accelDim);
	// 		//int ieqPerSample = (FlatoutputMonocycle::veloDim);

	// 		int j;
	// 		for (j = 0; j < FlatoutputMonocycle::accelDim; ++j)
	// 		{
	// 			const double absAcc = (acceleration(j, 0) > -acceleration(j, 0)) ? acceleration(j, 0) : -acceleration(j, 0);
	// 			result[nAcc + j + (i - 1)*ieqPerSample] = absAcc - thisPP->maxAcceleration(j, 0);
	// 			////std::cout << "G[" << nAcc + j + (i - 1)*ieqPerSample << "] " << result[nAcc + j + (i - 1)*ieqPerSample] << std::endl;
	// 		}

	// 		for (int k = j; k - j < FlatoutputMonocycle::veloDim; ++k)
	// 		{
	// 			const double absVel = (velocity(k - j, 0) > -velocity(k - j, 0)) ? velocity(k - j, 0) : -velocity(k - j, 0);
	// 			result[nAcc + k + (i - 1)*ieqPerSample] = absVel - thisPP->maxVelocity(k - j, 0);
	// 			////std::cout << "G[" << nAcc + k + (i - 1)*ieqPerSample << "] " << result[nAcc + k + (i - 1)*ieqPerSample] << std::endl;
	// 		}
	// 	}
	// 	//system("pause");
	// }

	// void PathPlannerRecHor::ineqFuncLS(unsigned m, double *result, unsigned n, const double* x, double* grad, void* data)
	// {
	// 	////std::cout << "ineqFuncLS" << std::endl;
	// 	// get this path planner pointer
	// 	PathPlannerRecHor *thisPP = static_cast< PathPlannerRecHor *>(data);

	// 	thisPP->eval_ineqLS(result, n, x, data);

	// 	////std::cout << "ineqFuncLS 2" << std::endl;

	// 	if (grad)
	// 	{
	// 		const double eps = sqrt(std::numeric_limits< double >::epsilon());
	// 		double *constrPre = new double[m];
	// 		double *constrPos = new double[m];
	// 		double *x1 = new double[n];
	// 		double h, dx;

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			x1[i] = x[i];
	// 		}

	// 		for (int i = 0; i < int(n); ++i)
	// 		{
	// 			//h = x[i] < eps*1e2 ? eps*eps*1e2 : eps*x[i];
	// 			h = x[i] < eps ? eps*eps : eps*x[i];
	// 			//h = eps;
	// 			x1[i] = x[i] + h;
	// 			thisPP->eval_ineqLS(constrPos, n, x1, data);
	// 			dx = x1[i] - x[i];
	// 			x1[i] = x[i] - h;
	// 			thisPP->eval_ineqLS(constrPre, n, x1, data);
	// 			dx = dx + x[i] - x1[i];
	// 			//dx = 2 * h;
	// 			x1[i] = x[i];
	// 			////std::cout << "INEQ: " << x[i] << ", " << eps << ", " << h << ", " << dx << std::endl;
	// 			for (int j = 0; j < m; ++j)
	// 			{
	// 				grad[j*n + i] = (constrPos[j] - constrPre[j]) / dx;
	// 				////std::cout << grad[j*n + i] << std::endl;
	// 			}
	// 		}
	// 		delete[] constrPre;
	// 		delete[] constrPos;
	// 		delete[] x1;
	// 	}
	// }



	// int PathPlannerRecHor::findspan(const double t)
	// {
	// 	int ret = 0;
	// 	//std::cout << "\n all knots :\n" << this->auxSpline.knots() << std::endl;
	// 	//std::cout << "\n knots@0 :\n" << this->auxSpline.knots()(0, 0) << std::endl;
	// 	//std::cout << "\n knots@4 :\n" << this->auxSpline.knots()(0, 4) << std::endl;
	// 	//std::cout << "\n knots@5 :\n" << this->auxSpline.knots()(0, 5) << std::endl;
	// 	//std::cout << "\nt :\n"  << t << std::endl;
	// 	while (ret <= _nCtrlPts - 1 &&  t >= this->auxSpline.knots()(0,ret))
	// 		ret++;
	// 	////std::cout << "\nspan:\n" << ret - 1 << std::endl;
	// 	return ret - 1;
	// }
}
// cmake:sourcegroup=PathPlanner