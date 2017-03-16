/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "nav-precomp.h" // Precomp header

#include <mrpt/nav/reactive/CAbstractNavigator.h>
#include <mrpt/poses/CPose2D.h>
#include <mrpt/math/geometry.h>
#include <mrpt/math/lightweight_geom_data.h>
#include <mrpt/utils/CConfigFileMemory.h>
#include <limits>

using namespace mrpt::nav;
using namespace std;

const double PREVIOUS_POSES_MAX_AGE = 20; // seconds

// Ctor: CAbstractNavigator::TNavigationParams
CAbstractNavigator::TNavigationParams::TNavigationParams() :
	target(0,0,0),
	targetAllowedDistance(0.5),
	targetIsRelative(false),
	targetIsIntermediaryWaypoint(false)
{
}

// Gets navigation params as a human-readable format:
std::string CAbstractNavigator::TNavigationParams::getAsText() const
{
	string s;
	s+= mrpt::format("navparams.target = (%.03f,%.03f,%.03f deg)\n", target.x, target.y,target.phi );
	s+= mrpt::format("navparams.targetAllowedDistance = %.03f\n", targetAllowedDistance );
	s+= mrpt::format("navparams.targetIsRelative = %s\n", targetIsRelative ? "YES":"NO");
	s+= mrpt::format("navparams.targetIsIntermediaryWaypoint = %s\n", targetIsIntermediaryWaypoint ? "YES":"NO");

	return s;
}

CAbstractNavigator::TRobotPoseVel::TRobotPoseVel() :
	pose(0,0,0),
	velGlobal(0,0,0),
	velLocal(0,0,0),
	timestamp(INVALID_TIMESTAMP)
{
}

/*---------------------------------------------------------------
							Constructor
  ---------------------------------------------------------------*/
CAbstractNavigator::CAbstractNavigator(CRobot2NavInterface &react_iterf_impl) :
	mrpt::utils::COutputLogger("MRPT_navigator"),
	m_lastNavigationState ( IDLE ),
	m_navigationEndEventSent(false),
	m_navigationState     ( IDLE ),
	m_navigationParams    ( nullptr ),
	m_lastNavTargetReached(false),
	m_robot               ( react_iterf_impl ),
	m_curPoseVel          (),
	m_last_curPoseVelUpdate_robot_time(-1e9),
	m_latestPoses         (),
	m_timlog_delays       (true, "CAbstractNavigator::m_timlog_delays")
{
	m_latestPoses.setInterpolationMethod(mrpt::poses::CPose3DInterpolator::imLinear2Neig);
	this->setVerbosityLevel(mrpt::utils::LVL_DEBUG);
}

// Dtor:
CAbstractNavigator::~CAbstractNavigator()
{
	mrpt::utils::delete_safe( m_navigationParams );
}

/*---------------------------------------------------------------
							cancel
  ---------------------------------------------------------------*/
void CAbstractNavigator::cancel()
{
	std::lock_guard<std::recursive_mutex> csl(m_nav_cs);
	MRPT_LOG_DEBUG("CAbstractNavigator::cancel() called.");
	m_navigationState = IDLE;
	m_lastNavTargetReached = false;
	this->stop(false /*not emergency*/);
}


/*---------------------------------------------------------------
							resume
  ---------------------------------------------------------------*/
void CAbstractNavigator::resume()
{
	std::lock_guard<std::recursive_mutex> csl(m_nav_cs);

	MRPT_LOG_DEBUG("[CAbstractNavigator::resume() called.");
	if ( m_navigationState == SUSPENDED )
		m_navigationState = NAVIGATING;
}


/*---------------------------------------------------------------
							suspend
  ---------------------------------------------------------------*/
void  CAbstractNavigator::suspend()
{
	std::lock_guard<std::recursive_mutex> csl(m_nav_cs);

	MRPT_LOG_DEBUG("CAbstractNavigator::suspend() called.");
	if ( m_navigationState == NAVIGATING )
		m_navigationState  = SUSPENDED;
}

void CAbstractNavigator::resetNavError()
{
	std::lock_guard<std::recursive_mutex> csl(m_nav_cs);

	MRPT_LOG_DEBUG("CAbstractNavigator::resetNavError() called.");
	if ( m_navigationState == NAV_ERROR )
		m_navigationState  = IDLE;
}

void CAbstractNavigator::loadConfigFile(const mrpt::utils::CConfigFileBase & c)
{
	MRPT_START

	params_abstract_navigator.loadFromConfigFile(c, "CAbstractNavigator");

	// At this point, all derived classes have already loaded their parameters.
	// Dump them to debug output:
	{
		mrpt::utils::CConfigFileMemory cfg_mem;
		this->saveConfigFile(cfg_mem);
		MRPT_LOG_INFO(cfg_mem.getContent());
	}

	MRPT_END
}

void CAbstractNavigator::saveConfigFile(mrpt::utils::CConfigFileBase & c) const
{
	params_abstract_navigator.saveToConfigFile(c, "CAbstractNavigator");
}

/*---------------------------------------------------------------
					navigationStep
  ---------------------------------------------------------------*/
void CAbstractNavigator::navigationStep()
{
	std::lock_guard<std::recursive_mutex> csl(m_nav_cs);
	mrpt::utils::CTimeLoggerEntry tle(m_timlog_delays, "CAbstractNavigator::navigationStep()");

	const TState prevState = m_navigationState;
	switch ( m_navigationState )
	{
	case IDLE:
	case SUSPENDED:
		try
		{
			// If we just arrived at this state, stop robot:
			if ( m_lastNavigationState == NAVIGATING )
			{
				MRPT_LOG_INFO("[CAbstractNavigator::navigationStep()] Navigation stopped.");
				// this->stop();  stop() is called by the method switching the "state", so we have more flexibility
				m_robot.stopWatchdog();
			}
		} catch (...) { }
		break;

	case NAV_ERROR:
		try
		{
			// Send end-of-navigation event:
			if ( m_lastNavigationState == NAVIGATING && m_navigationState == NAV_ERROR)
				m_robot.sendNavigationEndDueToErrorEvent();

			// If we just arrived at this state, stop the robot:
			if ( m_lastNavigationState == NAVIGATING )
			{
				MRPT_LOG_ERROR("[CAbstractNavigator::navigationStep()] Stoping Navigation due to a NAV_ERROR state!");
				this->stop(false /*not emergency*/);
				m_robot.stopWatchdog();
			}
		} catch (...) { }
		break;

	case NAVIGATING:
		try
		{
			if ( m_lastNavigationState != NAVIGATING )
			{
				MRPT_LOG_INFO("[CAbstractNavigator::navigationStep()] Starting Navigation. Watchdog initiated...\n");
				if (m_navigationParams)
					MRPT_LOG_DEBUG(mrpt::format("[CAbstractNavigator::navigationStep()] Navigation Params:\n%s\n", m_navigationParams->getAsText().c_str() ));

				m_robot.startWatchdog( 1000 );	// Watchdog = 1 seg
				m_latestPoses.clear(); // Clear cache of last poses.
				onStartNewNavigation();
			}

			// Have we just started the navigation?
			if ( m_lastNavigationState == IDLE )
				m_robot.sendNavigationStartEvent();

			/* ----------------------------------------------------------------
			        Get current robot dyn state:
				---------------------------------------------------------------- */
			updateCurrentPoseAndSpeeds();

			/* ----------------------------------------------------------------
		 			Have we reached the target location?
				---------------------------------------------------------------- */
			// Build a 2D segment from the current robot pose to the previous one:
			ASSERT_(!m_latestPoses.empty());
			const mrpt::math::TSegment2D seg_robot_mov = mrpt::math::TSegment2D(
				mrpt::math::TPoint2D(m_curPoseVel.pose),
				m_latestPoses.size()>1 ?
					mrpt::math::TPoint2D( (++m_latestPoses.rbegin())->second )
					:
					mrpt::math::TPoint2D((m_latestPoses.rbegin())->second)
			);

			const double targetDist = seg_robot_mov.distance( mrpt::math::TPoint2D(m_navigationParams->target) );

			// Should "End of navigation" event be sent??
			if (!m_navigationParams->targetIsIntermediaryWaypoint && !m_navigationEndEventSent && targetDist < params_abstract_navigator.dist_to_target_for_sending_event)
			{
				m_navigationEndEventSent = true;
				m_robot.sendNavigationEndEvent();
			}

			// Have we really reached the target?
			if ( targetDist < m_navigationParams->targetAllowedDistance )
			{
				m_lastNavTargetReached = true;

				if (!m_navigationParams->targetIsIntermediaryWaypoint) {
					this->stop(false /*not emergency*/);
				}
				m_navigationState = IDLE;
				logFmt(mrpt::utils::LVL_WARN, "Navigation target (%.03f,%.03f) was reached\n", m_navigationParams->target.x,m_navigationParams->target.y);

				if (!m_navigationParams->targetIsIntermediaryWaypoint && !m_navigationEndEventSent)
				{
					m_navigationEndEventSent = true;
					m_robot.sendNavigationEndEvent();
				}
				break;
			}

			// Check the "no approaching the target"-alarm:
			// -----------------------------------------------------------
			if (targetDist < m_badNavAlarm_minDistTarget )
			{
				m_badNavAlarm_minDistTarget = targetDist;
				m_badNavAlarm_lastMinDistTime =  mrpt::system::getCurrentTime();
			}
			else
			{
				// Too much time have passed?
				if (mrpt::system::timeDifference( m_badNavAlarm_lastMinDistTime, mrpt::system::getCurrentTime() ) > params_abstract_navigator.alarm_seems_not_approaching_target_timeout)
				{
					MRPT_LOG_WARN("--------------------------------------------\nWARNING: Timeout for approaching toward the target expired!! Aborting navigation!! \n---------------------------------\n");
					m_navigationState = NAV_ERROR;
					m_robot.sendWaySeemsBlockedEvent();
					break;
				}
			}

			// ==== The normal execution of the navigation: Execute one step ====
			performNavigationStep();

		}
		catch (std::exception &e)
		{
			cerr << "[CAbstractNavigator::navigationStep] Exception:\n" << e.what() << endl;
		}
		catch (...)
		{
			cerr << "[CAbstractNavigator::navigationStep] Unexpected exception.\n";
		}
		break;	// End case NAVIGATING
	};
	m_lastNavigationState = prevState;
}

void CAbstractNavigator::doEmergencyStop( const std::string &msg )
{
	try {
		this->stop(true /* emergency*/);
	}
	catch (...) { }
	m_navigationState = NAV_ERROR;
	MRPT_LOG_ERROR(msg);
}


void CAbstractNavigator::navigate(const CAbstractNavigator::TNavigationParams *params )
{
	std::lock_guard<std::recursive_mutex> csl(m_nav_cs);

	m_navigationEndEventSent = false;
	m_lastNavTargetReached = false;

	// Copy data:
	mrpt::utils::delete_safe(m_navigationParams);
	m_navigationParams = params->clone();

	// Transform: relative -> absolute, if needed.
	if ( m_navigationParams->targetIsRelative )
	{
		this->updateCurrentPoseAndSpeeds();

		const mrpt::poses::CPose2D relTarget(m_navigationParams->target);
		mrpt::poses::CPose2D absTarget;
		absTarget.composeFrom(m_curPoseVel.pose, relTarget);

		m_navigationParams->target = mrpt::math::TPose2D(absTarget);

		m_navigationParams->targetIsRelative = false; // Now it's not relative
	}

	// new state:
	m_navigationState = NAVIGATING;

	// Reset the bad navigation alarm:
	m_badNavAlarm_minDistTarget = std::numeric_limits<double>::max();
	m_badNavAlarm_lastMinDistTime = mrpt::system::getCurrentTime();
}

void CAbstractNavigator::updateCurrentPoseAndSpeeds()
{
	// Ignore calls too-close in time, e.g. from the navigationStep() methods of
	// AbstractNavigator and a derived, overriding class.
	const double robot_time_secs = m_robot.getNavigationTime();  // this is clockwall time for real robots, simulated time in simulators.

	const double MIN_TIME_BETWEEN_POSE_UPDATES = 20e-3;
	if (m_last_curPoseVelUpdate_robot_time >=.0)
	{
		const double last_call_age = robot_time_secs - m_last_curPoseVelUpdate_robot_time;
		if (last_call_age < MIN_TIME_BETWEEN_POSE_UPDATES)
		{
			MRPT_LOG_DEBUG_FMT("updateCurrentPoseAndSpeeds: ignoring call, since last call was only %f ms ago.", last_call_age*1e3);
			return;  // previous data is still valid: don't query the robot again
		}
	}

	{
		mrpt::utils::CTimeLoggerEntry tle(m_timlog_delays, "getCurrentPoseAndSpeeds()");
		if (!m_robot.getCurrentPoseAndSpeeds(m_curPoseVel.pose, m_curPoseVel.velGlobal, m_curPoseVel.timestamp))
		{
			m_navigationState = NAV_ERROR;
			try {
				this->stop(true /*emergency*/);
			}
			catch (...) {}
			MRPT_LOG_ERROR("ERROR calling m_robot.getCurrentPoseAndSpeeds, stopping robot and finishing navigation");
			throw std::runtime_error("ERROR calling m_robot.getCurrentPoseAndSpeeds, stopping robot and finishing navigation");
		}
	}
	m_curPoseVel.velLocal = m_curPoseVel.velGlobal;
	m_curPoseVel.velLocal.rotate(-m_curPoseVel.pose.phi);

	m_last_curPoseVelUpdate_robot_time = robot_time_secs;

	// Append to list of past poses:
	m_latestPoses.insert(m_curPoseVel.timestamp, mrpt::poses::CPose3D(mrpt::math::TPose3D(m_curPoseVel.pose)));

	// Purge old ones:
	while (m_latestPoses.size()>1 &&
		mrpt::system::timeDifference(m_latestPoses.begin()->first, m_latestPoses.rbegin()->first) > PREVIOUS_POSES_MAX_AGE)
	{
		m_latestPoses.erase(m_latestPoses.begin());
	}
	//MRPT_LOG_DEBUG_STREAM << "updateCurrentPoseAndSpeeds: " << m_latestPoses.size() << " poses in list of latest robot poses.";
}

bool CAbstractNavigator::changeSpeeds(const mrpt::kinematics::CVehicleVelCmd &vel_cmd)
{
	return m_robot.changeSpeeds(vel_cmd);
}
bool CAbstractNavigator::changeSpeedsNOP()
{
	return m_robot.changeSpeedsNOP();
}
bool CAbstractNavigator::stop(bool isEmergencyStop)
{
	return m_robot.stop(isEmergencyStop);
}

CAbstractNavigator::TAbstractNavigatorParams::TAbstractNavigatorParams() :
	dist_to_target_for_sending_event(0),
	alarm_seems_not_approaching_target_timeout(30)
{
}
void CAbstractNavigator::TAbstractNavigatorParams::loadFromConfigFile(const mrpt::utils::CConfigFileBase &c, const std::string &s)
{
	MRPT_LOAD_CONFIG_VAR_CS(dist_to_target_for_sending_event, double);
	MRPT_LOAD_CONFIG_VAR_CS(alarm_seems_not_approaching_target_timeout, double);
}
void CAbstractNavigator::TAbstractNavigatorParams::saveToConfigFile(mrpt::utils::CConfigFileBase &c, const std::string &s) const
{
	MRPT_SAVE_CONFIG_VAR_COMMENT(dist_to_target_for_sending_event, "Default value=0, means use the `targetAllowedDistance` passed by the user in the navigation request.");
	MRPT_SAVE_CONFIG_VAR_COMMENT(alarm_seems_not_approaching_target_timeout, "navigator timeout (seconds) [Default=30 sec]");
}
