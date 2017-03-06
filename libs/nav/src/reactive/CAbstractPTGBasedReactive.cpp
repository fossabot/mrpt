/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "nav-precomp.h" // Precomp header

#include <mrpt/nav/reactive/CAbstractPTGBasedReactive.h>
#include <mrpt/nav/holonomic/CHolonomicVFF.h>
#include <mrpt/nav/holonomic/CHolonomicND.h>
#include <mrpt/nav/holonomic/CHolonomicFullEval.h>
#include <mrpt/system/filesystem.h>
#include <mrpt/math/wrap2pi.h>
#include <mrpt/math/geometry.h>
#include <mrpt/math/ops_containers.h> // sum()
#include <mrpt/utils/printf_vector.h>
#include <mrpt/utils/metaprogramming.h>
#include <mrpt/utils/CFileGZOutputStream.h>
#include <mrpt/utils/CMemoryStream.h>
#include <mrpt/maps/CPointCloudFilterByDistance.h>
#include <limits>
#include <iomanip>
#include <array>

using namespace mrpt;
using namespace mrpt::poses;
using namespace mrpt::math;
using namespace mrpt::utils;
using namespace mrpt::nav;
using namespace std;

// ------ CAbstractPTGBasedReactive::TNavigationParamsPTG -----
std::string CAbstractPTGBasedReactive::TNavigationParamsPTG::getAsText() const
{
	std::string s = TNavigationParams::getAsText();
	s += "restrict_PTG_indices: ";
	s += mrpt::utils::sprintf_vector("%u ",this->restrict_PTG_indices);
	s += "\n";
	return s;
}

const double ESTIM_LOWPASSFILTER_ALPHA = 0.7;

// Ctor:
CAbstractPTGBasedReactive::CAbstractPTGBasedReactive(CRobot2NavInterface &react_iterf_impl, bool enableConsoleOutput, bool enableLogFile, const std::string &sLogDir):
	CWaypointsNavigator(react_iterf_impl),
	m_holonomicMethod            (),
	m_logFile                    (nullptr),
	m_prev_logfile               (nullptr),
	m_enableKeepLogRecords       (false),
	m_enableConsoleOutput        (enableConsoleOutput),
	m_init_done                  (false),
	m_timelogger                 (false), // default: disabled
	m_PTGsMustBeReInitialized    (true),
	meanExecutionTime            (ESTIM_LOWPASSFILTER_ALPHA, 0.1),
	meanTotalExecutionTime       (ESTIM_LOWPASSFILTER_ALPHA, 0.1),
	meanExecutionPeriod          (ESTIM_LOWPASSFILTER_ALPHA, 0.1),
	tim_changeSpeed_avr          (ESTIM_LOWPASSFILTER_ALPHA),
	timoff_obstacles_avr         (ESTIM_LOWPASSFILTER_ALPHA),
	timoff_curPoseAndSpeed_avr   (ESTIM_LOWPASSFILTER_ALPHA),
	timoff_sendVelCmd_avr        (ESTIM_LOWPASSFILTER_ALPHA),
	m_closing_navigator          (false),
	m_WS_Obstacles_timestamp     (INVALID_TIMESTAMP),
	m_infoPerPTG_timestamp       (INVALID_TIMESTAMP),
	m_lastTarget                 (0,0,0),
	m_navlogfiles_dir(sLogDir)
{
	this->enableLogFile( enableLogFile );
}

void CAbstractPTGBasedReactive::preDestructor()
{
	m_closing_navigator = true;

	// Wait to end of navigation (multi-thread...)
	m_nav_cs.enter();
	m_nav_cs.leave();

	// Just in case.
	try {
		this->stop(false /*not emergency*/);
	} catch (...) { }

	mrpt::utils::delete_safe(m_logFile);

	// Free holonomic method:
	this->deleteHolonomicObjects();
}

CAbstractPTGBasedReactive::~CAbstractPTGBasedReactive()
{
	// Just in case the user didn't call this, call it again to reduce memory leaks to those of the user:
	this->preDestructor();
}

void CAbstractPTGBasedReactive::initialize()
{
	mrpt::synch::CCriticalSectionLocker csl(&m_nav_cs);

	m_infoPerPTG_timestamp = INVALID_TIMESTAMP;

	ASSERT_(m_multiobjopt);
	m_multiobjopt->clear();

	// Compute collision grids:
	STEP1_InitPTGs();
}

/*---------------------------------------------------------------
						enableLogFile
  ---------------------------------------------------------------*/
void CAbstractPTGBasedReactive::enableLogFile(bool enable)
{
	mrpt::synch::CCriticalSectionLocker csl(&m_nav_cs);

	try
	{
		// Disable:
		// -------------------------------
		if (!enable)
		{
			if (m_logFile)
			{
				MRPT_LOG_DEBUG("[CAbstractPTGBasedReactive::enableLogFile] Stopping logging.");
				// Close file:
				delete m_logFile;
				m_logFile = nullptr;
			}
			else return;	// Already disabled.
		}
		else
		{	// Enable
			// -------------------------------
			if (m_logFile) return; // Already enabled:

			// Open file, find the first free file-name.
			char	aux[300];
			unsigned int nFile = 0;
			bool    free_name = false;

			mrpt::system::createDirectory(m_navlogfiles_dir);
			if (!mrpt::system::directoryExists(m_navlogfiles_dir)) {
				THROW_EXCEPTION_FMT("Could not create directory for navigation logs: `%s`", m_navlogfiles_dir.c_str());
			}

			while (!free_name)
			{
				nFile++;
				sprintf(aux, "%s/log_%03u.reactivenavlog", m_navlogfiles_dir.c_str(), nFile );
				free_name = !system::fileExists(aux);
			}

			// Open log file:
			{
				CFileGZOutputStream *fil = new CFileGZOutputStream();
				bool ok = fil->open(aux, 1 /* compress level */);
				if (!ok) {
					delete fil;
					THROW_EXCEPTION_FMT("Error opening log file: `%s`",aux);
				}
				else {
					m_logFile = fil;
				}
			}

			MRPT_LOG_DEBUG(mrpt::format("[CAbstractPTGBasedReactive::enableLogFile] Logging to file `%s`\n",aux));

		}
	} catch (std::exception &e) {
		MRPT_LOG_ERROR_FMT("[CAbstractPTGBasedReactive::enableLogFile] Exception: %s",e.what());
	}

}

void CAbstractPTGBasedReactive::getLastLogRecord( CLogFileRecord &o )
{
	mrpt::synch::CCriticalSectionLocker lock(&m_critZoneLastLog);
	o = lastLogRecord;
}

static std::string holoMethodEnum2ClassName(const THolonomicMethod method)
{
	std::string className;
	switch (method)
	{
	case hmSEARCH_FOR_BEST_GAP:  className = "CHolonomicND";  break;
	case hmVIRTUAL_FORCE_FIELDS: className = "CHolonomicVFF"; break;
	case hmFULL_EVAL: className = "CHolonomicFullEval"; break;
	default: THROW_EXCEPTION_FMT("Unknown Holonomic method: %u", static_cast<unsigned int>(method))
	};
	return className;
}

void CAbstractPTGBasedReactive::deleteHolonomicObjects()
{
	for (size_t i=0;i<m_holonomicMethod.size();i++)
		delete m_holonomicMethod[i];
	m_holonomicMethod.clear();
}

void CAbstractPTGBasedReactive::setHolonomicMethod(const std::string & method, const mrpt::utils::CConfigFileBase & ini)
{
	mrpt::synch::CCriticalSectionLocker csl(&m_nav_cs);

	this->deleteHolonomicObjects();
	const size_t nPTGs = this->getPTG_count();
	ASSERT_(nPTGs != 0);
	m_holonomicMethod.resize(nPTGs);

	for (size_t i = 0; i<nPTGs; i++)
	{
		m_holonomicMethod[i] = CAbstractHolonomicReactiveMethod::Create(method);
		if (!m_holonomicMethod[i])
			THROW_EXCEPTION_FMT("Non-registered holonomic method className=`%s`", method.c_str());

		m_holonomicMethod[i]->setAssociatedPTG(this->getPTG(i));
		m_holonomicMethod[i]->initialize(ini); // load params
	}
}

void CAbstractPTGBasedReactive::setHolonomicMethod(const THolonomicMethod method, const mrpt::utils::CConfigFileBase &ini)
{
	setHolonomicMethod(holoMethodEnum2ClassName(method), ini);
}

// The main method: executes one time-iteration of the reactive navigation algorithm.
void CAbstractPTGBasedReactive::performNavigationStep()
{
	// Security tests:
	if (m_closing_navigator) return;  // Are we closing in the main thread?
	if (!m_init_done) THROW_EXCEPTION("Have you called loadConfigFile() before?")
	ASSERT_(m_navigationParams)

	const size_t nPTGs = this->getPTG_count();

	// Whether to worry about log files:
	const bool fill_log_record = (m_logFile!=nullptr || m_enableKeepLogRecords);
	CLogFileRecord newLogRec;
	newLogRec.infoPerPTG.resize(nPTGs+1); /* +1: [N] is the "NOP cmdvel" option; not to be present in all log entries. */

	// At the beginning of each log file, add an introductory block explaining which PTGs are we using:
	{
		if (m_logFile && m_logFile!= m_prev_logfile)  // Only the first time
		{
			m_prev_logfile = m_logFile;
			for (size_t i=0;i<nPTGs;i++)
			{
				// If we make a direct copy (=) we will store the entire, heavy, collision grid.
				// Let's just store the parameters of each PTG by serializing it, so paths can be reconstructed
				// by invoking initialize()
				mrpt::utils::CMemoryStream buf;
				buf << *this->getPTG(i);
				buf.Seek(0);
				newLogRec.infoPerPTG[i].ptg = std::dynamic_pointer_cast<mrpt::nav::CParameterizedTrajectoryGenerator> ( buf.ReadObject() );
			}
		}
	}


	CTimeLoggerEntry tle1(m_timelogger,"navigationStep");

	try
	{
		totalExecutionTime.Tic(); // Start timer

		const mrpt::system::TTimeStamp tim_start_iteration = mrpt::system::now();

		// Compute target location relative to current robot pose:
		// ---------------------------------------------------------------------
		// Detect changes in target since last iteration (for NOP):
		const bool target_changed_since_last_iteration = m_navigationParams->target != m_lastTarget;
		m_lastTarget = m_navigationParams->target;

		STEP1_InitPTGs(); // Will only recompute if "m_PTGsMustBeReInitialized==true"

		// Update kinematic state in all PTGs:
		for (size_t i=0;i<nPTGs;i++)
			getPTG(i)->updateCurrentRobotVel(m_curPoseVel.velLocal);

		// STEP2: Load the obstacles and sort them in height bands.
		// -----------------------------------------------------------------------------
		if (! STEP2_SenseObstacles() )
		{
			doEmergencyStop("Error while loading and sorting the obstacles. Robot will be stopped.");
			if (fill_log_record)
			{
				CPose2D rel_cur_pose_wrt_last_vel_cmd_NOP, rel_pose_PTG_origin_wrt_sense_NOP;
				STEP8_GenerateLogRecord(newLogRec,
					mrpt::math::TPose2D() /*fake target*/,
					-1, // best_ptg_idx,
					m_robot.getEmergencyStopCmd(),
					nPTGs,
					false, //best_is_NOP_cmdvel,
					rel_cur_pose_wrt_last_vel_cmd_NOP,
					rel_pose_PTG_origin_wrt_sense_NOP,
					0, //executionTimeValue,
					0, //tim_changeSpeed,
					tim_start_iteration);
			}
			return;
		}

		// ------- start of motion decision zone ---------
		executionTime.Tic();

		// Round #1: As usual, pure reactive, evaluate all PTGs and all directions from scratch.
		// =========

		CPose2D rel_pose_PTG_origin_wrt_sense,relPoseSense, relPoseVelCmd;
		MRPT_TODO("port all delays-model to double and use robotTime() to make this compatible with faster-than-real-time simulators??");
		if (params_abstract_ptg_navigator.use_delays_model)
		{
			/*
			*                                          Delays model
			*
			* Event:          OBSTACLES_SENSED         RNAV_ITERATION_STARTS       GET_ROBOT_POSE_VEL         VEL_CMD_SENT_TO_ROBOT
			* Timestamp:  (m_WS_Obstacles_timestamp)   (tim_start_iteration)     (m_curPoseVelTimestamp)     ("tim_send_cmd_vel")
			* Delay                       | <---+--------------->|<--------------+-------->|                         |
			* estimator:                        |                |               |                                   |
			*                timoff_obstacles <-+                |               +--> timoff_curPoseVelAge           |
			*                                                    |<---------------------------------+--------------->|
			*                                                                                       +--> timoff_sendVelCmd_avr (estimation)
			*
			*                                                                                                |<-------------------->|
			*                                                                                                   tim_changeSpeed_avr (estim)
			*
			*                             |<-----------------------------------------------|-------------------------->|
			*  Relative poses:                              relPoseSense                           relPoseVelCmd
			*  Time offsets (signed):                       timoff_pose2sense                     timoff_pose2VelCmd
			*/
			const double timoff_obstacles = mrpt::system::timeDifference(tim_start_iteration, m_WS_Obstacles_timestamp);
			timoff_obstacles_avr.filter(timoff_obstacles);
			newLogRec.values["timoff_obstacles"] = timoff_obstacles;
			newLogRec.values["timoff_obstacles_avr"] = timoff_obstacles_avr.getLastOutput();
			newLogRec.timestamps["obstacles"] = m_WS_Obstacles_timestamp;

			const double timoff_curPoseVelAge = mrpt::system::timeDifference(tim_start_iteration, m_curPoseVel.timestamp);
			timoff_curPoseAndSpeed_avr.filter(timoff_curPoseVelAge);
			newLogRec.values["timoff_curPoseVelAge"] = timoff_curPoseVelAge;
			newLogRec.values["timoff_curPoseVelAge_avr"] = timoff_curPoseAndSpeed_avr.getLastOutput();

			// time offset estimations:
			const double timoff_pose2sense = timoff_obstacles - timoff_curPoseVelAge;

			double timoff_pose2VelCmd;
			timoff_pose2VelCmd = timoff_sendVelCmd_avr.getLastOutput() + 0.5*tim_changeSpeed_avr.getLastOutput() - timoff_curPoseVelAge;
			newLogRec.values["timoff_pose2sense"] = timoff_pose2sense;
			newLogRec.values["timoff_pose2VelCmd"] =  timoff_pose2VelCmd;

			if (std::abs(timoff_pose2sense) > 1.25) MRPT_LOG_WARN_FMT("timoff_pose2sense=%e is too large! Path extrapolation may be not accurate.", timoff_pose2sense);
			if (std::abs(timoff_pose2VelCmd) > 1.25) MRPT_LOG_WARN_FMT("timoff_pose2VelCmd=%e is too large! Path extrapolation may be not accurate.", timoff_pose2VelCmd);

			// Path extrapolation: robot relative poses along current path estimation:
			robotPoseExtrapolateIncrement(m_curPoseVel.velLocal, timoff_pose2sense, relPoseSense);
			robotPoseExtrapolateIncrement(m_curPoseVel.velLocal, timoff_pose2VelCmd, relPoseVelCmd);
			// relative pose for PTGs:
			rel_pose_PTG_origin_wrt_sense = relPoseVelCmd - relPoseSense;

			// logging:
			newLogRec.relPoseSense = relPoseSense;
			newLogRec.relPoseVelCmd = relPoseVelCmd;
		}
		else {
			// No delays model: poses to their default values.
		}

		const TPose2D relTarget = TPose2D(CPose2D(m_navigationParams->target) - (CPose2D(m_curPoseVel.pose) + relPoseVelCmd));

		m_infoPerPTG.assign(nPTGs+1, TInfoPerPTG());  // reset contents
		m_infoPerPTG_timestamp = tim_start_iteration;
		vector<TCandidateMovementPTG> candidate_movs(nPTGs+1); // the last extra one is for the evaluation of "NOP motion command" choice.

		for (size_t indexPTG=0;indexPTG<nPTGs;indexPTG++)
		{
			CParameterizedTrajectoryGenerator * ptg = getPTG(indexPTG);
			TInfoPerPTG &ipf = m_infoPerPTG[indexPTG];

			// The picked movement in TP-Space (to be determined by holonomic method below)
			TCandidateMovementPTG &cm = candidate_movs[indexPTG];

			ASSERT_(m_navigationParams);
			build_movement_candidate(
				ptg, indexPTG,
				relTarget, rel_pose_PTG_origin_wrt_sense,
				ipf, cm,
				newLogRec, false /* this is a regular PTG reactive case */,
				m_holonomicMethod[indexPTG],
				tim_start_iteration,
				*m_navigationParams
				);
		} // end for each PTG

		// Round #2: Evaluate dont sending any new velocity command ("NOP" motion)
		// =========
		// This approach is only possible if:
		bool NOP_not_too_old = true;
		double NOP_max_time = -1.0, NOP_At = -1.0;
		const bool can_do_nop_motion = (m_lastSentVelCmd.isValid() &&
			!target_changed_since_last_iteration &&
			getPTG(m_lastSentVelCmd.ptg_index)->supportVelCmdNOP()) &&
			(NOP_not_too_old = (NOP_At=mrpt::system::timeDifference(m_lastSentVelCmd.tim_send_cmd_vel, tim_start_iteration)) < (NOP_max_time=getPTG(m_lastSentVelCmd.ptg_index)->maxTimeInVelCmdNOP(m_lastSentVelCmd.ptg_alpha_index)) );

		if (!NOP_not_too_old) {
			newLogRec.additional_debug_msgs["PTG_cont"] = mrpt::format("PTG-continuation not allowed: previous command timed-out (At=%.03f > Max_At=%.03f)", NOP_At, NOP_max_time);
		}

		CPose2D rel_cur_pose_wrt_last_vel_cmd_NOP, rel_pose_PTG_origin_wrt_sense_NOP;

		if (can_do_nop_motion)
		{
			// Add the estimation of how long it takes to run the changeSpeeds() callback (usually a tiny period):
			const mrpt::system::TTimeStamp tim_send_cmd_vel_corrected =
				mrpt::system::timestampAdd(
					m_lastSentVelCmd.tim_send_cmd_vel,
					tim_changeSpeed_avr.getLastOutput());

			CPose3D robot_pose3d_at_send_cmd;
			bool valid_pose;
			m_latestPoses.interpolate(tim_send_cmd_vel_corrected, robot_pose3d_at_send_cmd, valid_pose);
			if (valid_pose)
			{
				const CPose2D robot_pose_at_send_cmd = CPose2D(robot_pose3d_at_send_cmd);

				CParameterizedTrajectoryGenerator * ptg = getPTG(m_lastSentVelCmd.ptg_index);
				ASSERT_(ptg!=nullptr);
				ptg->updateCurrentRobotVel(m_lastSentVelCmd.poseVel.velLocal);

				const TPose2D relTarget_NOP = TPose2D(CPose2D(m_navigationParams->target) - robot_pose_at_send_cmd);
				rel_pose_PTG_origin_wrt_sense_NOP = robot_pose_at_send_cmd - (CPose2D(m_curPoseVel.pose) + relPoseSense);
				rel_cur_pose_wrt_last_vel_cmd_NOP = CPose2D(m_curPoseVel.pose) - robot_pose_at_send_cmd;

				if (fill_log_record)
				{
					newLogRec.additional_debug_msgs["rel_cur_pose_wrt_last_vel_cmd_NOP(interp)"] = rel_cur_pose_wrt_last_vel_cmd_NOP.asString();
					newLogRec.additional_debug_msgs["robot_pose_at_send_cmd(interp)"] = robot_pose_at_send_cmd.asString();
				}

				ASSERT_(m_navigationParams);
				build_movement_candidate(
					ptg, m_lastSentVelCmd.ptg_index,
					relTarget_NOP, rel_pose_PTG_origin_wrt_sense_NOP,
					m_infoPerPTG[nPTGs], candidate_movs[nPTGs],
					newLogRec, true /* this is the PTG continuation (NOP) choice */,
					m_holonomicMethod[m_lastSentVelCmd.ptg_index],
					tim_start_iteration,
					*m_navigationParams,
					rel_cur_pose_wrt_last_vel_cmd_NOP);

			} // end valid interpolated origin pose
			else
			{
				// Can't interpolate pose, hence can't evaluate NOP:
				candidate_movs[nPTGs].speed = -0.01; // <0 means inviable movement
			}
		} //end can_do_NOP_motion

		// Evaluate all the candidates and pick the "best" one, using 
		// the user-defined multiobjective optimizer
		// --------------------------------------------------------------
		CMultiObjectiveMotionOptimizerBase::TResultInfo mo_info;
		ASSERT_(m_multiobjopt);
		int best_ptg_idx = m_multiobjopt->decide(candidate_movs, mo_info);

		if (fill_log_record && mo_info.final_evaluation.size()== newLogRec.infoPerPTG.size())
		{
			for (unsigned int i = 0; i < newLogRec.infoPerPTG.size(); i++) {
				newLogRec.infoPerPTG[i].evaluation = mo_info.final_evaluation[i];
			}
		}

		// Pick best movement (or null if none is good)
		const TCandidateMovementPTG * selectedHolonomicMovement = nullptr;
		if (best_ptg_idx >= 0) {
			selectedHolonomicMovement = &candidate_movs[best_ptg_idx];
		}

		// If the selected PTG is (N+1), it means the NOP cmd. vel is selected as the best alternative, i.e. do NOT send any new motion command.
		const bool best_is_NOP_cmdvel =  (best_ptg_idx==int(nPTGs));

		// ---------------------------------------------------------------------
		//				SEND MOVEMENT COMMAND TO THE ROBOT
		// ---------------------------------------------------------------------
		mrpt::kinematics::CVehicleVelCmd::Ptr new_vel_cmd;
		if (best_is_NOP_cmdvel)
		{
			// Notify the robot that we want it to keep executing the last cmdvel:
			if (!this->changeSpeedsNOP())
			{
				doEmergencyStop("\nERROR calling changeSpeedsNOP()!! Stopping robot and finishing navigation\n");
				if(fill_log_record)
				{
					STEP8_GenerateLogRecord(newLogRec,
						relTarget,
						best_ptg_idx,
						m_robot.getEmergencyStopCmd(),
						nPTGs,
						best_is_NOP_cmdvel,
						rel_cur_pose_wrt_last_vel_cmd_NOP,
						rel_pose_PTG_origin_wrt_sense_NOP,
						0, //executionTimeValue,
						0, //tim_changeSpeed,
						tim_start_iteration);
				}
				return;
			}
		}
		else
		{
			// STEP7: Get the non-holonomic movement command.
			// ---------------------------------------------------------------------
			double cmd_vel_speed_ratio = 1.0;
			if (selectedHolonomicMovement)
			{
				CTimeLoggerEntry tle(m_timelogger, "navigationStep.selectedHolonomicMovement");
				cmd_vel_speed_ratio = generate_vel_cmd(*selectedHolonomicMovement, new_vel_cmd);
				ASSERT_(new_vel_cmd);
			}

			if (!new_vel_cmd /* which means best_PTG_eval==.0*/ || new_vel_cmd->isStopCmd()) {
				MRPT_LOG_DEBUG("Best velocity command is STOP (no way found), calling robot.stop()");
				this->stop(true /* emergency */);  // don't call doEmergencyStop() here since that will stop navigation completely
				new_vel_cmd = m_robot.getEmergencyStopCmd();
				m_lastSentVelCmd.reset();
			}
			else
			{
				mrpt::system::TTimeStamp tim_send_cmd_vel;
				{
					mrpt::utils::CTimeLoggerEntry tle(m_timlog_delays, "changeSpeeds()");
					tim_send_cmd_vel = mrpt::system::now();
					newLogRec.timestamps["tim_send_cmd_vel"] = tim_send_cmd_vel;
					if (!this->changeSpeeds(*new_vel_cmd))
					{
						doEmergencyStop("\nERROR calling changeSpeeds()!! Stopping robot and finishing navigation\n");
						if (fill_log_record)
						{
							new_vel_cmd = m_robot.getEmergencyStopCmd();
							STEP8_GenerateLogRecord(newLogRec,
								relTarget,
								best_ptg_idx,
								new_vel_cmd,
								nPTGs,
								best_is_NOP_cmdvel,
								rel_cur_pose_wrt_last_vel_cmd_NOP,
								rel_pose_PTG_origin_wrt_sense_NOP,
								0, //executionTimeValue,
								0, //tim_changeSpeed,
								tim_start_iteration);
						}
						return;
					}
				}
				// Save last sent cmd:
				m_lastSentVelCmd.speed_scale = cmd_vel_speed_ratio;
				m_lastSentVelCmd.ptg_index = best_ptg_idx;
				m_lastSentVelCmd.ptg_alpha_index = selectedHolonomicMovement ?
					selectedHolonomicMovement->PTG->alpha2index(selectedHolonomicMovement->direction)
					:
					0;

				m_lastSentVelCmd.colfreedist_move_k = best_ptg_idx >= 0 ?
					m_infoPerPTG[best_ptg_idx].TP_Obstacles[m_lastSentVelCmd.ptg_alpha_index]
					:
					.0;

				m_lastSentVelCmd.tp_target_k = selectedHolonomicMovement ?
					selectedHolonomicMovement->PTG->alpha2index(m_infoPerPTG[best_ptg_idx].target_alpha) :
					0;

				m_lastSentVelCmd.poseVel = m_curPoseVel;
				m_lastSentVelCmd.tim_send_cmd_vel = tim_send_cmd_vel;


				// Update delay model:
				const double timoff_sendVelCmd = mrpt::system::timeDifference(tim_start_iteration, tim_send_cmd_vel);
				timoff_sendVelCmd_avr.filter(timoff_sendVelCmd);
				newLogRec.values["timoff_sendVelCmd"] = timoff_sendVelCmd;
				newLogRec.values["timoff_sendVelCmd_avr"] = timoff_sendVelCmd_avr.getLastOutput();
			}
		}

		// ------- end of motion decision zone ---------


		// Statistics:
		// ----------------------------------------------------
		const double executionTimeValue = executionTime.Tac();
		meanExecutionTime.filter(executionTimeValue);
		meanTotalExecutionTime.filter(totalExecutionTime.Tac());

		const double tim_changeSpeed = m_timlog_delays.getLastTime("changeSpeeds()");
		tim_changeSpeed_avr.filter(tim_changeSpeed);

		// Running period estim:
		const double period_tim= timerForExecutionPeriod.Tac();
		if (period_tim > 1.5* meanExecutionPeriod.getLastOutput()) {
			MRPT_LOG_WARN_FMT("Timing warning: Suspicious executionPeriod=%.03f ms is far above the average of %.03f ms", 1e3*period_tim, meanExecutionPeriod.getLastOutput()*1e3);
		}
		meanExecutionPeriod.filter(period_tim);
		timerForExecutionPeriod.Tic();


		if (m_enableConsoleOutput)
		{
			MRPT_LOG_DEBUG(mrpt::format(
				"CMD: %s "
				"speedScale=%.04f "
				"T=%.01lfms Exec:%.01lfms|%.01lfms "
				"PTG#%i\n",
					new_vel_cmd ? new_vel_cmd->asString().c_str() : "NOP",
					selectedHolonomicMovement ? selectedHolonomicMovement->speed : .0,
					1000.0*meanExecutionPeriod.getLastOutput(),
					1000.0*meanExecutionTime.getLastOutput(),
					1000.0*meanTotalExecutionTime.getLastOutput(),
					best_ptg_idx
					) );
		}
		if (fill_log_record)
		{
			STEP8_GenerateLogRecord(newLogRec,
				relTarget,
				best_ptg_idx,
				new_vel_cmd,
				nPTGs,
				best_is_NOP_cmdvel,
				rel_cur_pose_wrt_last_vel_cmd_NOP,
				rel_pose_PTG_origin_wrt_sense_NOP,
				executionTimeValue,
				tim_changeSpeed,
				tim_start_iteration);
		}
	}
	catch (std::exception &e) {
		doEmergencyStop(std::string("[CAbstractPTGBasedReactive::performNavigationStep] Stopping robot and finishing navigation due to exception:\n") + std::string(e.what()));
	}
	catch (...) {
		doEmergencyStop("[CAbstractPTGBasedReactive::performNavigationStep] Stopping robot and finishing navigation due to untyped exception." );
	}
}


void CAbstractPTGBasedReactive::STEP8_GenerateLogRecord(CLogFileRecord &newLogRec,const TPose2D& relTarget,int nSelectedPTG, const mrpt::kinematics::CVehicleVelCmd::Ptr &new_vel_cmd, const int nPTGs, const bool best_is_NOP_cmdvel, const mrpt::poses::CPose2D &rel_cur_pose_wrt_last_vel_cmd_NOP, const mrpt::poses::CPose2D &rel_pose_PTG_origin_wrt_sense_NOP, const double executionTimeValue, const double tim_changeSpeed, const mrpt::system::TTimeStamp &tim_start_iteration)
{
	// ---------------------------------------
	// STEP8: Generate log record
	// ---------------------------------------
	m_timelogger.enter("navigationStep.populate_log_info");

	this->loggingGetWSObstaclesAndShape(newLogRec);

	newLogRec.robotOdometryPose   = m_curPoseVel.pose;
	newLogRec.WS_target_relative  = TPoint2D(relTarget);
	newLogRec.nSelectedPTG        = nSelectedPTG;
	newLogRec.cur_vel             = m_curPoseVel.velGlobal;
	newLogRec.cur_vel_local       = m_curPoseVel.velLocal;
	newLogRec.cmd_vel = new_vel_cmd;
	newLogRec.values["estimatedExecutionPeriod"] = meanExecutionPeriod.getLastOutput();
	newLogRec.values["executionTime"] = executionTimeValue;
	newLogRec.values["executionTime_avr"] = meanExecutionTime.getLastOutput();
	newLogRec.values["time_changeSpeeds()"] = tim_changeSpeed;
	newLogRec.values["time_changeSpeeds()_avr"] = tim_changeSpeed_avr.getLastOutput();
	newLogRec.values["CWaypointsNavigator::navigationStep()"] = m_timlog_delays.getLastTime("CWaypointsNavigator::navigationStep()");
	newLogRec.values["CAbstractNavigator::navigationStep()"] = m_timlog_delays.getLastTime("CAbstractNavigator::navigationStep()");
	newLogRec.timestamps["tim_start_iteration"] = tim_start_iteration;
	newLogRec.timestamps["curPoseAndVel"] = m_curPoseVel.timestamp;
	newLogRec.nPTGs = nPTGs;

	// NOP mode  stuff:
	newLogRec.rel_cur_pose_wrt_last_vel_cmd_NOP = rel_cur_pose_wrt_last_vel_cmd_NOP;
	newLogRec.rel_pose_PTG_origin_wrt_sense_NOP = rel_pose_PTG_origin_wrt_sense_NOP;
	newLogRec.ptg_index_NOP = best_is_NOP_cmdvel ? m_lastSentVelCmd.ptg_index : -1;
	newLogRec.ptg_last_k_NOP = m_lastSentVelCmd.ptg_alpha_index;
	newLogRec.ptg_last_curRobotVelLocal = m_lastSentVelCmd.poseVel.velLocal;

	// Last entry in info-per-PTG:
	{
		CLogFileRecord::TInfoPerPTG &ipp = *newLogRec.infoPerPTG.rbegin();
		if (!ipp.HLFR) ipp.HLFR = CLogFileRecord_VFF::Create();
	}

	m_timelogger.leave("navigationStep.populate_log_info");

	//  Save to log file:
	// --------------------------------------
	{
		mrpt::utils::CTimeLoggerEntry tle(m_timelogger,"navigationStep.write_log_file");
		if (m_logFile) (*m_logFile) << newLogRec;
	}
	// Set as last log record
	{
		mrpt::synch::CCriticalSectionLocker lock_log(&m_critZoneLastLog);    // Lock
		lastLogRecord = newLogRec; // COPY
	}
}

void CAbstractPTGBasedReactive::calc_move_candidate_scores(
	TCandidateMovementPTG         & cm,
	const std::vector<double>        & in_TPObstacles,
	const mrpt::nav::ClearanceDiagram & in_clearance,
	const mrpt::math::TPose2D  & WS_Target,
	const mrpt::math::TPoint2D & TP_Target,
	CLogFileRecord::TInfoPerPTG & log,
	CLogFileRecord & newLogRec,
	const bool this_is_PTG_continuation,
	const mrpt::poses::CPose2D & rel_cur_pose_wrt_last_vel_cmd_NOP,
	const unsigned int ptg_idx4weights,
	const mrpt::system::TTimeStamp tim_start_iteration)
{
	MRPT_START;

	const double   ref_dist      = cm.PTG->getRefDistance();
	const double   target_dir    = (TP_Target.x!=0 || TP_Target.y!=0) ? atan2( TP_Target.y, TP_Target.x) : 0.0;
	const int      target_k = static_cast<int>( cm.PTG->alpha2index( target_dir ) );
	const double   target_d_norm   = TP_Target.norm();
	// Picked movement direction:
	const int      move_k   = static_cast<int>( cm.PTG->alpha2index( cm.direction ) );

	// Coordinates of the trajectory end for the given PTG and "alpha":
	const double d = std::min( in_TPObstacles[ move_k ], 0.99*target_d_norm);
	uint32_t nStep;
	bool pt_in_range = cm.PTG->getPathStepForDist(move_k, d, nStep);
	ASSERT_(pt_in_range)

	mrpt::math::TPose2D pose;
	cm.PTG->getPathPose(move_k, nStep,pose);

	// Start storing params in the candidate move structure: 
	cm.props["ptg_idx"] = ptg_idx4weights;
	cm.props["ref_dist"] = ref_dist;
	cm.props["target_dir"] = target_dir;
	cm.props["target_k"] = target_k;
	cm.props["target_d_norm"] = target_d_norm;
	cm.props["move_k"] = move_k;
	cm.props["is_PTG_cont"] = this_is_PTG_continuation ? 1 : 0;
	cm.props["num_paths"] = in_TPObstacles.size();
	cm.props["WS_target_x"] = WS_Target.x;
	cm.props["WS_target_y"] = WS_Target.y;
	cm.props["robpose_x"] = pose.x;
	cm.props["robpose_y"] = pose.y;
	cm.props["robpose_phi"] = pose.phi;
	cm.props["ptg_priority"] = cm.PTG->getScorePriority() *  cm.PTG->evalPathRelativePriority(target_k, target_d_norm);

	// Factor 1: Free distance for the chosen PTG and "alpha" in the TP-Space:
	// ----------------------------------------------------------------------
	double & colfree = cm.props["colision_free_distance"];
	if (move_k == target_k && target_d_norm>.0 && in_TPObstacles[move_k]>target_d_norm+0.05 /*small margin*/) {
		// If we head straight to target, don't count the possible collisions ahead:
		colfree = mrpt::utils::saturate_val(in_TPObstacles[move_k] / (target_d_norm + 0.05 /* give a minimum margin */), 0.0, 1.0);
	}
	else {
		// Normal case: distance to collision:
		colfree = in_TPObstacles[move_k];
	}

	// Special case for NOP motion cmd:
	// consider only the empty space *after* the current robot pose, which is not at the origin.
	if (this_is_PTG_continuation)
	{
		int cur_k=0;
		double cur_norm_d=.0;
		bool is_exact, is_time_based = false;
		uint32_t cur_ptg_step = 0;

		// Use: time-based prediction for shorter distances, PTG inverse mapping-based for longer ranges:
		const double maxD = params_abstract_ptg_navigator.max_dist_for_timebased_path_prediction;
		if (std::abs(rel_cur_pose_wrt_last_vel_cmd_NOP.x()) > maxD || std::abs(rel_cur_pose_wrt_last_vel_cmd_NOP.y()) > maxD) {
			is_exact = cm.PTG->inverseMap_WS2TP(rel_cur_pose_wrt_last_vel_cmd_NOP.x(), rel_cur_pose_wrt_last_vel_cmd_NOP.y(), cur_k, cur_norm_d);
		}
		else {
			// Use time: 
			is_time_based = true;
			is_exact = true; // well, sort of...
			const double NOP_At = m_lastSentVelCmd.speed_scale * mrpt::system::timeDifference(m_lastSentVelCmd.tim_send_cmd_vel, tim_start_iteration);
			newLogRec.additional_debug_msgs["PTG_eval.NOP_At"] = mrpt::format("%.06f s",NOP_At);
			cur_k = move_k;
			cur_ptg_step = mrpt::utils::round(NOP_At / cm.PTG->getPathStepDuration());
			cur_norm_d = cm.PTG->getPathDist(cur_k, cur_ptg_step) / cm.PTG->getRefDistance();
			{
				const double cur_a = cm.PTG->index2alpha(cur_k);
				log.TP_Robot.x = cos(cur_a)*cur_norm_d;
				log.TP_Robot.y = sin(cur_a)*cur_norm_d;
				cm.starting_robot_dir = cur_a;
				cm.starting_robot_dist = cur_norm_d;
			}
		}

		if (!is_exact)
		{
			// Don't trust this step: we are not 100% sure of the robot pose in TP-Space for this "PTG continuation" step:
			cm.speed = -0.01; // this enforces a 0 global evaluation score
			newLogRec.additional_debug_msgs["PTG_eval"] = "PTG-continuation not allowed, cur. pose out of PTG domain.";
			return;
		}
		bool WS_point_is_unique = true; 
		if (!is_time_based)
		{
			bool ok1 = cm.PTG->getPathStepForDist(m_lastSentVelCmd.ptg_alpha_index, cur_norm_d * cm.PTG->getRefDistance(), cur_ptg_step);
			if (ok1) {
				// Check bijective:
				WS_point_is_unique = cm.PTG->isBijectiveAt(cur_k, cur_ptg_step);
				const uint32_t predicted_step = mrpt::system::timeDifference(m_lastSentVelCmd.tim_send_cmd_vel, mrpt::system::now()) / cm.PTG->getPathStepDuration();
				WS_point_is_unique = WS_point_is_unique && cm.PTG->isBijectiveAt(move_k, predicted_step);
				newLogRec.additional_debug_msgs["PTG_eval.bijective"] = mrpt::format("isBijectiveAt(): k=%i step=%i -> %s", (int)cur_k, (int)cur_ptg_step, WS_point_is_unique ? "yes" : "no");

				if (!WS_point_is_unique)
				{
					// Don't trust direction:
					cur_k = move_k;
					cur_ptg_step = predicted_step;
					cur_norm_d = cm.PTG->getPathDist(cur_k, cur_ptg_step);
				}
				{
					const double cur_a = cm.PTG->index2alpha(cur_k);
					log.TP_Robot.x = cos(cur_a)*cur_norm_d;
					log.TP_Robot.y = sin(cur_a)*cur_norm_d;
					cm.starting_robot_dir = cur_a;
					cm.starting_robot_dist = cur_norm_d;
				}

				mrpt::math::TPose2D predicted_rel_pose;
				cm.PTG->getPathPose(m_lastSentVelCmd.ptg_alpha_index, cur_ptg_step, predicted_rel_pose);
				const CPose2D predicted_pose_global = CPose2D(m_lastSentVelCmd.poseVel.pose) + CPose2D(predicted_rel_pose);
				const double predicted2real_dist = predicted_pose_global.distance2DTo(m_curPoseVel.pose.x, m_curPoseVel.pose.y);
				newLogRec.additional_debug_msgs["PTG_eval.lastCmdPose(raw)"] = m_lastSentVelCmd.poseVel.pose.asString();
				newLogRec.additional_debug_msgs["PTG_eval.PTGcont"] = mrpt::format("mismatchDistance=%.03f cm", 1e2*predicted2real_dist);

				if (predicted2real_dist > params_abstract_ptg_navigator.max_distance_predicted_actual_path)
				{
					cm.speed = -0.01; // this enforces a 0 global evaluation score
					newLogRec.additional_debug_msgs["PTG_eval"] = "PTG-continuation not allowed, mismatchDistance above threshold.";
					return;
				}
			}
			else {
				cm.speed = -0.01; // this enforces a 0 global evaluation score
				newLogRec.additional_debug_msgs["PTG_eval"] = "PTG-continuation not allowed, couldn't get PTG step for cur. robot pose.";
				return;
			}
		}

		// Path following isn't perfect: we can't be 100% sure of whether the robot followed exactly
		// the intended path (`kDirection`), or if it's actually a bit shifted, as reported in `cur_k`.
		// Take the least favorable case.
		// [Update] Do this only when the PTG gave us a unique-mapped WS<->TPS point:

		colfree = WS_point_is_unique ?
			std::min(in_TPObstacles[move_k], in_TPObstacles[cur_k])
			:
			in_TPObstacles[move_k];

		// Only discount free space if there was a real obstacle, not the "end of path" due to limited refDistance.
		if (colfree < 0.99) {
			colfree -= cur_norm_d;
		}
	}

	//// Factor 2: Distance in sectors:
	//// -------------------------------------------
	////int dif = std::abs(TargetSector - kDirection);
	////if ( dif > int(nSectors/2)) dif = nSectors - dif;
	//m_expr_var_k = move_k;
	//m_expr_var_k_target = target_k;
	//m_expr_var_num_paths = in_TPObstacles.size();

	//// Was: eval_factors[SCOREIDX_TPS_DIRECTION] = exp(-std::abs( dif / (nSectors/10.0)));
	//eval_factors[SCOREIDX_TPS_DIRECTION] = PIMPL_GET_CONSTREF(exprtk::expression<double>, m_expr_score2_formula).value();

	// Factor 3: Angle between the robot at the end of the chosen trajectory and the target
	// -------------------------------------------------------------------------------------
	//double t_ang = atan2( WS_Target.y - pose.y, WS_Target.x - pose.x );
	//t_ang -= pose.phi;
	//mrpt::math::wrapToPiInPlace(t_ang);

	//eval_factors[SCOREIDX_ORIENTATION_AT_END] = exp(-square( t_ang / (0.5*M_PI)) );

	// Factor4: Decrease in euclidean distance between (x,y) and the target:
	//  Moving away of the target is negatively valued.
	// ---------------------------------------------------------------------------
	cm.props["dist_eucl_final"] = std::hypot(WS_Target.x- pose.x, WS_Target.y- pose.y);

	// Factor5: Hysteresis:
	// -----------------------------------------------------
	double &hysteresis = cm.props["hysteresis"];
	hysteresis = .0;

	if (cm.PTG->supportVelCmdNOP())
	{
		hysteresis = this_is_PTG_continuation ? 1.0 : 0.;
	}
	else if (m_last_vel_cmd)
	{
		mrpt::kinematics::CVehicleVelCmd::Ptr desired_cmd;
		desired_cmd = cm.PTG->directionToMotionCommand(move_k);
		const mrpt::kinematics::CVehicleVelCmd *ptr1 = m_last_vel_cmd.get();
		const mrpt::kinematics::CVehicleVelCmd *ptr2 = desired_cmd.get();
		if(typeid(*ptr1) == typeid(*ptr2)){
			ASSERT_EQUAL_(m_last_vel_cmd->getVelCmdLength(), desired_cmd->getVelCmdLength());

			double simil_score = 0.5;
			for (size_t i = 0; i < desired_cmd->getVelCmdLength(); i++)
			{
				const double scr = exp(-std::abs(desired_cmd->getVelCmdElement(i) - m_last_vel_cmd->getVelCmdElement(i)) / 0.20);
				mrpt::utils::keep_min(simil_score, scr);
			}
			hysteresis = simil_score;
		}
	}

	// Factor6: clearance
	// -----------------------------------------------------
	double &clearance = cm.props["clearance"];
	clearance = in_clearance.getClearance(move_k, target_d_norm*1.01, false /* spot, dont interpolate */ );

#if 0 // ->leave this to the multiobjective optimizer
	// Don't trust PTG continuation if we are too close to obstacles:
	if (this_is_PTG_continuation &&
		std::min(eval_factors[SCOREIDX_COLISION_FREE_DISTANCE], eval_factors[SCOREIDX_CLEARANCE]) < params_abstract_ptg_navigator.min_normalized_free_space_for_ptg_continuation)
	{
		newLogRec.additional_debug_msgs["PTG_eval"] = "PTG-continuation not allowed, too close to obstacles.";
		cm.speed = -0.01; // this enforces a 0 global evaluation score
		return;
	}
#endif

	// Factor: ETA (Estimated Time of Arrival to target or to closest obstacle, whatever it's first)
	// -----------------------------------------------------
	double &eta = cm.props["eta"];
	eta = .0;
	if (cm.PTG && cm.speed>.0) // for valid cases only
	{
		// OK, we have a direct path to target without collisions.
		const double path_len_meters = d * ref_dist;

		// Calculate their ETA
		uint32_t target_step;
		bool valid_step = cm.PTG->getPathStepForDist(move_k, path_len_meters, target_step);
		if (valid_step)
		{
			eta = cm.PTG->getPathStepDuration() * target_step /* PTG original time to get to target point */
				* cm.speed /* times the speed scale factor*/;

			double discount_time = .0;
			if (this_is_PTG_continuation) 
			{
				// Heuristic: discount the time already executed.
				// Note that hm.speed above scales the overall path time using the current speed scale, not the exact
				// integration over the past timesteps. It's an approximation, probably good enough...
				
				MRPT_TODO("support for simulation time here too!");
				discount_time = mrpt::system::timeDifference(m_lastSentVelCmd.tim_send_cmd_vel, tim_start_iteration);
			}
			eta -= discount_time; // This could even become negative if the approximation is poor...
		}
	}

	//  SAVE LOG
	log.evalFactors = cm.props;

	MRPT_END;
}

double CAbstractPTGBasedReactive::generate_vel_cmd( const TCandidateMovementPTG &in_movement, mrpt::kinematics::CVehicleVelCmd::Ptr &new_vel_cmd )
{
	mrpt::utils::CTimeLoggerEntry tle(m_timelogger, "generate_vel_cmd");
	double cmdvel_speed_scale = 1.0;
	try
	{
		if (in_movement.speed == 0)
		{
			// The robot will stop:
			new_vel_cmd = in_movement.PTG->getSupportedKinematicVelocityCommand();
			new_vel_cmd->setToStop();
		}
		else
		{
			// Take the normalized movement command:
			new_vel_cmd = in_movement.PTG->directionToMotionCommand( in_movement.PTG->alpha2index( in_movement.direction ) );

			// Scale holonomic speeds to real-world one:
			new_vel_cmd->cmdVel_scale(in_movement.speed);
			cmdvel_speed_scale *= in_movement.speed;

			if (!m_last_vel_cmd) // first iteration? Use default values:
				m_last_vel_cmd = in_movement.PTG->getSupportedKinematicVelocityCommand();

			// Honor user speed limits & "blending":
			const double beta = meanExecutionPeriod.getLastOutput() / (meanExecutionPeriod.getLastOutput() + params_abstract_ptg_navigator.speedfilter_tau);
			cmdvel_speed_scale *= new_vel_cmd->cmdVel_limits(*m_last_vel_cmd, beta, params_abstract_ptg_navigator.robot_absolute_speed_limits);
		}

		m_last_vel_cmd = new_vel_cmd; // Save for filtering in next step
	}
	catch (std::exception &e)
	{
		MRPT_LOG_ERROR_STREAM << "[CAbstractPTGBasedReactive::generate_vel_cmd] Exception: " << e.what();
	}
	return cmdvel_speed_scale;
}

bool CAbstractPTGBasedReactive::impl_waypoint_is_reachable(const mrpt::math::TPoint2D &wp) const
{
	MRPT_START;

	const size_t N = this->getPTG_count();
	if (m_infoPerPTG.size()<N ||
		m_infoPerPTG_timestamp == INVALID_TIMESTAMP ||
		mrpt::system::timeDifference(m_infoPerPTG_timestamp, mrpt::system::now() ) > 0.5
		)
		return false; // We didn't run yet or obstacle info is old

	for (size_t i=0;i<N;i++)
	{
		const CParameterizedTrajectoryGenerator* ptg = getPTG(i);

		const std::vector<double> & tp_obs = m_infoPerPTG[i].TP_Obstacles; // normalized distances
		if (tp_obs.size() != ptg->getPathCount() )
			continue; // May be this PTG has not been used so far? (Target out of domain,...)

		int wp_k;
		double wp_norm_d;
		bool is_into_domain = ptg->inverseMap_WS2TP(wp.x,wp.y,  wp_k, wp_norm_d);
		if (!is_into_domain)
			continue;

		ASSERT_(wp_k<int(tp_obs.size()));

		const double collision_free_dist = tp_obs[wp_k];
		if (collision_free_dist>1.01*wp_norm_d)
			return true; // free path found to target
	}

	return false; // no way found
	MRPT_END;
}

bool CAbstractPTGBasedReactive::STEP2_SenseObstacles()
{
	return implementSenseObstacles(m_WS_Obstacles_timestamp);
}

void CAbstractPTGBasedReactive::robotPoseExtrapolateIncrement(const mrpt::math::TTwist2D & globalVel, const double time_offset, mrpt::poses::CPose2D & out_pose)
{
	out_pose.x(globalVel.vx * time_offset);
	out_pose.y(globalVel.vy * time_offset);
	out_pose.phi( globalVel.omega * time_offset );
}

void CAbstractPTGBasedReactive::onStartNewNavigation()
{
	m_last_curPoseVelUpdate_robot_time = -1e9;
	m_lastSentVelCmd.reset();

	CWaypointsNavigator::onStartNewNavigation(); // Call base method we override
}

CAbstractPTGBasedReactive::TSentVelCmd::TSentVelCmd()
{
	reset();
}
void CAbstractPTGBasedReactive::TSentVelCmd::reset()
{
	ptg_index = -1;
	ptg_alpha_index = -1;
	tp_target_k = -1;
	tim_send_cmd_vel = INVALID_TIMESTAMP;
	poseVel = TRobotPoseVel();
	colfreedist_move_k = .0;
	speed_scale = 1.0;
}
bool CAbstractPTGBasedReactive::TSentVelCmd::isValid() const
{
	return this->poseVel.timestamp != INVALID_TIMESTAMP;
}


void CAbstractPTGBasedReactive::build_movement_candidate(
	CParameterizedTrajectoryGenerator * ptg,
	const size_t indexPTG,
	const TPose2D &relTarget,
	const CPose2D &rel_pose_PTG_origin_wrt_sense,
	TInfoPerPTG &ipf,
	TCandidateMovementPTG &cm,
	CLogFileRecord &newLogRec,
	const bool this_is_PTG_continuation,
	mrpt::nav::CAbstractHolonomicReactiveMethod *holoMethod,
	const mrpt::system::TTimeStamp tim_start_iteration,
	const TNavigationParams &navp,
	const mrpt::poses::CPose2D &rel_cur_pose_wrt_last_vel_cmd_NOP
	)
{
	ASSERT_(ptg);

	const size_t idx_in_log_infoPerPTGs = this_is_PTG_continuation ? getPTG_count() : indexPTG;

	CHolonomicLogFileRecord::Ptr HLFR;
	cm.PTG = ptg;

	// If the user doesn't want to use this PTG, just mark it as invalid:
	ipf.valid_TP = true;
	{
		const TNavigationParamsPTG * navpPTG = dynamic_cast<const TNavigationParamsPTG*>(&navp);
		if (navpPTG && !navpPTG->restrict_PTG_indices.empty())
		{
			bool use_this_ptg = false;
			for (size_t i = 0; i < navpPTG->restrict_PTG_indices.size() && !use_this_ptg; i++) {
				if (navpPTG->restrict_PTG_indices[i] == indexPTG)
					use_this_ptg = true;
			}
			ipf.valid_TP = use_this_ptg;
		}
	}

	double timeForTPObsTransformation = .0, timeForHolonomicMethod = .0;

	// Normal PTG validity filter: check if target falls into the PTG domain:
	if (ipf.valid_TP)
	{
		ipf.valid_TP = ptg->inverseMap_WS2TP(relTarget.x, relTarget.y, ipf.target_k, ipf.target_dist);
	}

	if (!ipf.valid_TP)
	{
		ipf.target_k = 0;
		ipf.target_dist = 0;

		newLogRec.additional_debug_msgs[mrpt::format("mov_candidate_%u", static_cast<unsigned int>(indexPTG))] = "PTG discarded since target is out of domain.";

		{   // Invalid PTG (target out of reachable space):
			// - holonomicMovement= Leave default values
			HLFR = CLogFileRecord_VFF::Create();
		}
	}
	else
	{
		ipf.target_alpha = ptg->index2alpha(ipf.target_k);
		ipf.TP_Target.x = cos(ipf.target_alpha) * ipf.target_dist;
		ipf.TP_Target.y = sin(ipf.target_alpha) * ipf.target_dist;

		//  STEP3(b): Build TP-Obstacles
		// -----------------------------------------------------------------------------
		{
			tictac.Tic();

			// Initialize TP-Obstacles:
			const size_t Ki = ptg->getAlphaValuesCount();
			ptg->initTPObstacles(ipf.TP_Obstacles);
			if (params_abstract_ptg_navigator.evaluate_clearance) {
				ptg->initClearanceDiagram(ipf.clearance);
			}

			// Implementation-dependent conversion:
			STEP3_WSpaceToTPSpace(indexPTG, ipf.TP_Obstacles, ipf.clearance, -rel_pose_PTG_origin_wrt_sense, params_abstract_ptg_navigator.evaluate_clearance);

			if (params_abstract_ptg_navigator.evaluate_clearance) {
				ptg->updateClearancePost(ipf.clearance, ipf.TP_Obstacles);
			}

			// Distances in TP-Space are normalized to [0,1]:
			const double _refD = 1.0 / ptg->getRefDistance();
			for (size_t i = 0; i < Ki; i++) ipf.TP_Obstacles[i] *= _refD;

			timeForTPObsTransformation = tictac.Tac();
			if (m_timelogger.isEnabled())
				m_timelogger.registerUserMeasure("navigationStep.STEP3_WSpaceToTPSpace", timeForTPObsTransformation);
		}

		//  STEP4: Holonomic navigation method
		// -----------------------------------------------------------------------------
		if (!this_is_PTG_continuation)
		{
			tictac.Tic();

			ASSERT_(holoMethod);
			// Don't slow down if we are approaching a target that is not the final waypoint:
			holoMethod->enableApproachTargetSlowDown( !navp.targetIsIntermediaryWaypoint );

			// Prepare holonomic algorithm call:
			CAbstractHolonomicReactiveMethod::NavInput ni;
			ni.clearance = &ipf.clearance;
			ni.maxObstacleDist = 1.0;
			ni.maxRobotSpeed = 1.0; // So, we use a normalized max speed here.
			ni.obstacles = ipf.TP_Obstacles;  // Normalized [0,1]
			ni.target = ipf.TP_Target; // Normalized [0,1]

			CAbstractHolonomicReactiveMethod::NavOutput no;

			holoMethod->navigate(ni, no);

			// Extract resuls:
			cm.direction = no.desiredDirection;
			cm.speed = no.desiredSpeed;
			HLFR = no.logRecord;

			// Security: Scale down the velocity when heading towards obstacles,
			//  such that it's assured that we never go thru an obstacle!
			const int kDirection = static_cast<int>(cm.PTG->alpha2index(cm.direction));
			double obsFreeNormalizedDistance = ipf.TP_Obstacles[kDirection];

			// Take into account the future robot pose after NOP motion iterations to slow down accordingly *now*
			if (ptg->supportVelCmdNOP()) {
				const double v = ::hypot(m_curPoseVel.velLocal.vx, m_curPoseVel.velLocal.vy);
				const double d = v * ptg->maxTimeInVelCmdNOP(kDirection);
				obsFreeNormalizedDistance = std::min(obsFreeNormalizedDistance, std::max(0.90, obsFreeNormalizedDistance - d) );
			}

			double velScale = 1.0;
			ASSERT_(params_abstract_ptg_navigator.secure_distance_end > params_abstract_ptg_navigator.secure_distance_start);
			if (obsFreeNormalizedDistance < params_abstract_ptg_navigator.secure_distance_end)
			{
				if (obsFreeNormalizedDistance <= params_abstract_ptg_navigator.secure_distance_start)
					velScale = 0.0; // security stop
				else velScale = (obsFreeNormalizedDistance - params_abstract_ptg_navigator.secure_distance_start) / (params_abstract_ptg_navigator.secure_distance_end - params_abstract_ptg_navigator.secure_distance_start);
			}

			// Scale:
			cm.speed *= velScale;

			timeForHolonomicMethod = tictac.Tac();
			if (m_timelogger.isEnabled())
				m_timelogger.registerUserMeasure("navigationStep.STEP4_HolonomicMethod", timeForHolonomicMethod);
		}
		else
		{
			// "NOP cmdvel" case: don't need to re-run holo algorithm, just keep the last selection:
			cm.direction = ptg->index2alpha( m_lastSentVelCmd.ptg_alpha_index );
			cm.speed = 1.0; // Not used.
		}

		// STEP5: Evaluate each movement to assign them a "evaluation" value.
		// ---------------------------------------------------------------------
		{
			CTimeLoggerEntry tle(m_timelogger, "navigationStep.calc_move_candidate_scores");

			calc_move_candidate_scores(
				cm,
				ipf.TP_Obstacles,
				ipf.clearance,
				relTarget,
				ipf.TP_Target,
				newLogRec.infoPerPTG[idx_in_log_infoPerPTGs], newLogRec,
				this_is_PTG_continuation, rel_cur_pose_wrt_last_vel_cmd_NOP,
				indexPTG, 
				tim_start_iteration);

			// Store NOP related extra vars:
			cm.props["original_col_free_dist"] =
				this_is_PTG_continuation ?
				m_lastSentVelCmd.colfreedist_move_k
				:
				.0;
		}


	} // end "valid_TP"

	// Logging:
	const bool fill_log_record = (m_logFile != nullptr || m_enableKeepLogRecords);
	if (fill_log_record)
	{
		CLogFileRecord::TInfoPerPTG &ipp = newLogRec.infoPerPTG[idx_in_log_infoPerPTGs];
		if (!this_is_PTG_continuation)
		     ipp.PTG_desc = ptg->getDescription();
		else ipp.PTG_desc = mrpt::format("NOP cmdvel (prev PTG idx=%u)", static_cast<unsigned int>(m_lastSentVelCmd.ptg_index) );

		metaprogramming::copy_container_typecasting(ipf.TP_Obstacles, ipp.TP_Obstacles);
		ipp.clearance = ipf.clearance;
		ipp.TP_Target = ipf.TP_Target;
		ipp.HLFR = HLFR;
		ipp.desiredDirection = cm.direction;
		ipp.desiredSpeed = cm.speed;
		ipp.timeForTPObsTransformation = timeForTPObsTransformation;
		ipp.timeForHolonomicMethod = timeForHolonomicMethod;
	}
}

void CAbstractPTGBasedReactive::TAbstractPTGNavigatorParams::loadFromConfigFile(const mrpt::utils::CConfigFileBase & c, const std::string & s)
{
	MRPT_START;

	robot_absolute_speed_limits.loadConfigFile(c, s);
	
	MRPT_LOAD_CONFIG_VAR_REQUIRED_CS(holonomic_method, string);
	MRPT_LOAD_CONFIG_VAR_REQUIRED_CS(motion_decider_method, string);
	MRPT_LOAD_CONFIG_VAR_REQUIRED_CS(ref_distance, double);
	MRPT_LOAD_CONFIG_VAR_CS(speedfilter_tau, double);
	MRPT_LOAD_CONFIG_VAR_CS(secure_distance_start, double);
	MRPT_LOAD_CONFIG_VAR_CS(secure_distance_end, double);
	MRPT_LOAD_CONFIG_VAR_CS(use_delays_model, bool);
	MRPT_LOAD_CONFIG_VAR_CS(max_distance_predicted_actual_path, double);
	MRPT_LOAD_CONFIG_VAR_CS(min_normalized_free_space_for_ptg_continuation, double);
	MRPT_LOAD_CONFIG_VAR_CS(enable_obstacle_filtering, bool);
	MRPT_LOAD_CONFIG_VAR_CS(evaluate_clearance, bool);
	MRPT_LOAD_CONFIG_VAR_CS(max_dist_for_timebased_path_prediction, double);

	MRPT_END;
}

void CAbstractPTGBasedReactive::TAbstractPTGNavigatorParams::saveToConfigFile(mrpt::utils::CConfigFileBase & c, const std::string & s) const
{
	robot_absolute_speed_limits.saveToConfigFile(c, s);

	// Build list of known holo methods:
	string lstHoloStr = "# List of known classes:\n";
	{
		const auto lst = mrpt::utils::getAllRegisteredClassesChildrenOf(CLASS_ID(CAbstractHolonomicReactiveMethod));
		for (const auto &c : lst) 
			lstHoloStr += string("# - `") + string(c->className) + string("`\n");
	}
	MRPT_SAVE_CONFIG_VAR_COMMENT(holonomic_method, string("C++ class name of the holonomic navigation method to run in the transformed TP-Space.\n")+ lstHoloStr);

	// Build list of known decider methods:
	string lstDecidersStr = "# List of known classes:\n";
	{
		const auto lst = mrpt::utils::getAllRegisteredClassesChildrenOf(CLASS_ID(CMultiObjectiveMotionOptimizerBase));
		for (const auto &c : lst)
			lstDecidersStr += string("# - `") + string(c->className) + string("`\n");
	}
	MRPT_SAVE_CONFIG_VAR_COMMENT(motion_decider_method, string("C++ class name of the motion decider method.\n") + lstDecidersStr);

	MRPT_SAVE_CONFIG_VAR_COMMENT(ref_distance, "Maximum distance up to obstacles will be considered (D_{max} in papers).");
	MRPT_SAVE_CONFIG_VAR_COMMENT(speedfilter_tau, "Time constant (in seconds) for the low-pass filter applied to kinematic velocity commands (default=0: no filtering)");
	MRPT_SAVE_CONFIG_VAR_COMMENT(secure_distance_start, "In normalized distance [0,1], start/end of a ramp function that scales the holonomic navigator output velocity.");
	MRPT_SAVE_CONFIG_VAR_COMMENT(secure_distance_end, "In normalized distance [0,1], start/end of a ramp function that scales the holonomic navigator output velocity.");
	MRPT_SAVE_CONFIG_VAR_COMMENT(use_delays_model, "Whether to use robot pose inter/extrapolation to improve accuracy (Default:false)");
	MRPT_SAVE_CONFIG_VAR_COMMENT(max_distance_predicted_actual_path, "Max distance [meters] to discard current PTG and issue a new vel cmd (default= 0.05)");
	MRPT_SAVE_CONFIG_VAR_COMMENT(min_normalized_free_space_for_ptg_continuation, "Min normalized dist [0,1] after current pose in a PTG continuation to allow it.");
	MRPT_SAVE_CONFIG_VAR_COMMENT(enable_obstacle_filtering, "Enabled obstacle filtering (params in its own section)");
	MRPT_SAVE_CONFIG_VAR_COMMENT(evaluate_clearance, "Enable exact computation of clearance (default=false)");
	MRPT_SAVE_CONFIG_VAR_COMMENT(max_dist_for_timebased_path_prediction, "Max dist [meters] to use time-based path prediction for NOP evaluation");
}

CAbstractPTGBasedReactive::TAbstractPTGNavigatorParams::TAbstractPTGNavigatorParams() :
	holonomic_method(),
	ptg_cache_files_directory("."),
	ref_distance(4.0),
	speedfilter_tau(0.0),
	secure_distance_start(0.05),
	secure_distance_end(0.20),
	use_delays_model(false),
	max_distance_predicted_actual_path(0.15),
	min_normalized_free_space_for_ptg_continuation(0.2),
	robot_absolute_speed_limits(),
	enable_obstacle_filtering(true),
	evaluate_clearance(false),
	max_dist_for_timebased_path_prediction(2.0)
{
}

void CAbstractPTGBasedReactive::loadConfigFile(const mrpt::utils::CConfigFileBase & c)
{
	MRPT_START;
	m_PTGsMustBeReInitialized = true;

	// At this point, we have been called from the derived class, who must be already 
	// loaded all its specific params, including PTGs.

	// Load my params:
	params_abstract_ptg_navigator.loadFromConfigFile(c, "CAbstractPTGBasedReactive");

	// Filtering:
	if (params_abstract_ptg_navigator.enable_obstacle_filtering)
	{
		mrpt::maps::CPointCloudFilterByDistance *filter = new mrpt::maps::CPointCloudFilterByDistance;
		m_WS_filter = mrpt::maps::CPointCloudFilterBase::Ptr(filter);
		filter->options.loadFromConfigFile(c,"CPointCloudFilterByDistance");
	}
	else
	{
		m_WS_filter.reset();
	}

	// Movement chooser:
	m_multiobjopt = CMultiObjectiveMotionOptimizerBase::Ptr(CMultiObjectiveMotionOptimizerBase::Create(params_abstract_ptg_navigator.motion_decider_method));
	if (!m_multiobjopt)
		THROW_EXCEPTION_FMT("Non-registered CMultiObjectiveMotionOptimizerBase className=`%s`", params_abstract_ptg_navigator.motion_decider_method.c_str());

	m_multiobjopt->loadConfigFile(c);


	// Holo method:
	this->setHolonomicMethod(params_abstract_ptg_navigator.holonomic_method, c);
	ASSERT_(!m_holonomicMethod.empty())

	CWaypointsNavigator::loadConfigFile(c); // Load parent params

	m_init_done = true; // If we reached this point without an exception, all is good.
	MRPT_END;
}

void CAbstractPTGBasedReactive::saveConfigFile(mrpt::utils::CConfigFileBase & c) const
{
	CWaypointsNavigator::saveConfigFile(c);
	params_abstract_ptg_navigator.saveToConfigFile(c, "CAbstractPTGBasedReactive");

	// Filtering:
	{
		mrpt::maps::CPointCloudFilterByDistance filter;
		filter.options.saveToConfigFile(c, "CPointCloudFilterByDistance");
	}

	// Holo method:
	if (!m_holonomicMethod.empty() && m_holonomicMethod[0])
	{
		// Save my current settings:
		m_holonomicMethod[0]->saveConfigFile(c);
	}
	else
	{
		// save options of ALL known methods:
		const auto lst = mrpt::utils::getAllRegisteredClassesChildrenOf(CLASS_ID(CAbstractHolonomicReactiveMethod));
		for (const auto &cl : lst) {
			mrpt::utils::CObject::Ptr obj = mrpt::utils::CObject::Ptr(cl->createObject());
			CAbstractHolonomicReactiveMethod * holo = dynamic_cast<CAbstractHolonomicReactiveMethod *>(obj.get());
			if (holo) {
				holo->saveConfigFile(c);
			}
		}
	}

	// Decider method:
	if (m_multiobjopt)
	{
		// Save my current settings:
		m_multiobjopt->saveConfigFile(c);
	}
	else
	{
		// save options of ALL known methods:
		const auto lst = mrpt::utils::getAllRegisteredClassesChildrenOf(CLASS_ID(CMultiObjectiveMotionOptimizerBase));
		for (const auto &cl : lst) {
			mrpt::utils::CObject::Ptr obj = mrpt::utils::CObject::Ptr(cl->createObject());
			CMultiObjectiveMotionOptimizerBase * momo = dynamic_cast<CMultiObjectiveMotionOptimizerBase *>(obj.get());
			if (momo) {
				momo->saveConfigFile(c);
			}
		}
	}
}
