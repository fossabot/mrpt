/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */
#pragma once

#include <mrpt/nav/reactive/CRobot2NavInterface.h>
#include <mrpt/utils/COutputLogger.h>
#include <mrpt/utils/CTimeLogger.h>
#include <mrpt/utils/TEnumType.h>
#include <mrpt/utils/CLoadableOptions.h>
#include <mrpt/poses/CPose3DInterpolator.h>
#include <mrpt/obs/obs_frwds.h>

#include <mrpt/nav/link_pragmas.h>

#include <mutex>

namespace mrpt
{
  namespace nav
  {
	/** This is the base class for any reactive/planned navigation system. See derived classes.
	 *
	 * How to use:
	 *  - A class derived from `CRobot2NavInterface` with callbacks must be defined by the user and provided to the constructor.
	 *  - `navigationStep()` must be called periodically in order to effectively run the navigation. This method will internally call the callbacks to gather sensor data and robot positioning data.
	 *
	 * It implements the following state machine (see CAbstractNavigator::getCurrentState() ), taking into account the extensions described in CWaypointsNavigator
	 *  \dot
	 *  digraph CAbstractNavigator_States {
	 *      IDLE; NAVIGATING; SUSPENDED; NAV_ERROR;
	 *      IDLE -> NAVIGATING [ label="CAbstractNavigator::navigate()"];
	 *      IDLE -> NAVIGATING [ label="CWaypointsNavigator::navigateWaypoints()" ];
	 *      NAVIGATING -> IDLE [ label="Final target reached" ];
	 *      NAVIGATING -> IDLE [ label="CAbstractNavigator::cancel()" ];
	 *      NAVIGATING -> NAV_ERROR [ label="Upon sensor errors, timeout,..." ];
	 *      NAVIGATING -> SUSPENDED [ label="CAbstractNavigator::suspend()" ];
	 *      SUSPENDED -> NAVIGATING [ label="CAbstractNavigator::resume()" ];
	 *      NAV_ERROR -> IDLE [ label="CAbstractNavigator::resetNavError()" ];
	 *  }
	 *  \enddot
	 *
	 * \sa CWaypointsNavigator, CReactiveNavigationSystem, CRobot2NavInterface, all children classes
	 *  \ingroup nav_reactive
	 */
	class NAV_IMPEXP CAbstractNavigator : public mrpt::utils::COutputLogger
	{
	public:
		CAbstractNavigator( CRobot2NavInterface &robot_interface_impl );  //!< ctor
		virtual ~CAbstractNavigator(); //!< dtor

		/** The struct for configuring navigation requests. Used in CAbstractPTGBasedReactive::navigate() */
		struct NAV_IMPEXP TNavigationParams
		{
			mrpt::math::TPose2D target;  //!< Coordinates of desired target location. Heading may be ignored by some reactive implementations.
			float               targetAllowedDistance;    //!< (Default=0.5 meters) Allowed distance to target in order to end the navigation.
			bool                targetIsRelative;  //!< (Default=false) Whether the \a target coordinates are in global coordinates (false) or are relative to the current robot pose (true).

			/** (Default=false) If true, the behavior changes in these aspects:
			 * - The robot will *not* slow down when approaching the target.
			 * - Event callback `sendWaypointReachedEvent()` will be called instead of `sendNavigationEndEvent()`
			 */
			bool  targetIsIntermediaryWaypoint;

			TNavigationParams(); //!< Ctor with default values
			virtual ~TNavigationParams() {}
			virtual std::string getAsText() const; //!< Gets navigation params as a human-readable format
			virtual TNavigationParams* clone() const { return new TNavigationParams(*this); }
		};

		/** \name Navigation control API
		  * @{ */

		/** Loads all params from a file. To be called before initialize(). 
		  * Each derived class *MUST* load its own parameters, and then call *ITS PARENT'S* overriden method to ensure all params are loaded. */
		virtual void loadConfigFile(const mrpt::utils::CConfigFileBase &c);
		/** Saves all current options to a config file.
		  * Each derived class *MUST* save its own parameters, and then call *ITS PARENT'S* overriden method to ensure all params are saved. */
		virtual void saveConfigFile(mrpt::utils::CConfigFileBase &c) const;

		virtual void initialize() = 0; //!<  Must be called before any other navigation command
		virtual void navigationStep(); //!< This method must be called periodically in order to effectively run the navigation

		/** Navigation request to a single target location. It starts a new navigation.
		  * \param[in] params Pointer to structure with navigation info (its contents will be copied, so the original can be freely destroyed upon return if it was dynamically allocated.)
		  * \note A pointer is used so the passed object can be polymorphic with derived types.
		  */
		virtual void navigate( const TNavigationParams *params );

		virtual void cancel(); //!< Cancel current navegation.
		virtual void resume(); //!< Continues with suspended navigation. \sa suspend
		virtual void suspend(); //!< Suspend current navegation. \sa resume
		virtual void resetNavError(); //!< Resets a `NAV_ERROR` state back to `IDLE`

		/** The different states for the navigation system. */
		enum TState {
			IDLE=0,
			NAVIGATING,
			SUSPENDED,
			NAV_ERROR
		};

		/** Returns the current navigator state. */
		inline TState getCurrentState() const { return m_navigationState; }

		/** @}*/

		struct NAV_IMPEXP TAbstractNavigatorParams : public mrpt::utils::CLoadableOptions
		{
			double dist_to_target_for_sending_event;  //!< Default value=0, means use the "targetAllowedDistance" passed by the user in the navigation request.
			double alarm_seems_not_approaching_target_timeout; //!< navigator timeout (seconds) [Default=30 sec]

			virtual void loadFromConfigFile(const mrpt::utils::CConfigFileBase &c, const std::string &s) MRPT_OVERRIDE;
			virtual void saveToConfigFile(mrpt::utils::CConfigFileBase &c, const std::string &s) const MRPT_OVERRIDE;
			TAbstractNavigatorParams();
		};

		TAbstractNavigatorParams params_abstract_navigator;

		/** Gives access to a const-ref to the internal time logger used to estimate delays \sa getTimeLogger() in derived classes */
		const mrpt::utils::CTimeLogger & getDelaysTimeLogger() const { return m_timlog_delays; }

	private:
		TState  m_lastNavigationState; //!< Last internal state of navigator:
		bool    m_navigationEndEventSent; //!< Will be false until the navigation end is sent, and it is reset with each new command

	protected:
		/** To be implemented in derived classes */
		virtual void  performNavigationStep( )=0;

		/** Called whenever a new navigation has been started. Can be used to reset state variables, etc. */
		virtual void onStartNewNavigation() = 0;

		/** Call to the robot getCurrentPoseAndSpeeds() and updates members m_curPose,m_curVel and m_curVelLocal accordingly. */
		void updateCurrentPoseAndSpeeds();

		/** Stops the robot and set navigation state to error */
		void doEmergencyStop( const std::string &msg );

		virtual bool changeSpeeds(const mrpt::kinematics::CVehicleVelCmd &vel_cmd); //!< Default: forward call to m_robot.changeSpeed(). Can be overriden.
		virtual bool changeSpeedsNOP(); //!< Default: forward call to m_robot.changeSpeedsNOP(). Can be overriden.
		virtual bool stop(bool isEmergencyStop); //!< Default: forward call to m_robot.stop(). Can be overriden.

		TState             m_navigationState;  //!< Current internal state of navigator:
		TNavigationParams  *m_navigationParams;  //!< Current navigation parameters
		bool                m_lastNavTargetReached; //!< Whether the last target was reached successfully.

		CRobot2NavInterface   &m_robot; //!< The navigator-robot interface.

		std::recursive_mutex m_nav_cs; //!< mutex for all navigation methods

		struct NAV_IMPEXP TRobotPoseVel
		{
			mrpt::math::TPose2D  pose;
			mrpt::math::TTwist2D velGlobal, velLocal;
			mrpt::system::TTimeStamp timestamp;
			TRobotPoseVel();
		};

		TRobotPoseVel m_curPoseVel; //!< Current robot pose (updated in CAbstractNavigator::navigationStep() )
		double  m_last_curPoseVelUpdate_robot_time;
		mrpt::poses::CPose3DInterpolator m_latestPoses; //!< Latest robot poses and velocities (updated in CAbstractNavigator::navigationStep() )

		mrpt::utils::CTimeLogger m_timlog_delays; //!< Time logger to collect delay-related stats

		/** For sending an alarm (error event) when it seems that we are not approaching toward the target in a while... */
		double                   m_badNavAlarm_minDistTarget;
		mrpt::system::TTimeStamp m_badNavAlarm_lastMinDistTime;

	public:
		MRPT_MAKE_ALIGNED_OPERATOR_NEW
	};
  }

	// Specializations MUST occur at the same namespace:
	namespace utils
	{
		template <>
		struct TEnumTypeFiller<mrpt::nav::CAbstractNavigator::TState>
		{
			typedef mrpt::nav::CAbstractNavigator::TState enum_t;
			static void fill(bimap<enum_t, std::string>  &m_map)
			{
				m_map.insert(mrpt::nav::CAbstractNavigator::IDLE, "IDLE");
				m_map.insert(mrpt::nav::CAbstractNavigator::NAVIGATING, "NAVIGATING");
				m_map.insert(mrpt::nav::CAbstractNavigator::SUSPENDED, "SUSPENDED");
				m_map.insert(mrpt::nav::CAbstractNavigator::NAV_ERROR, "NAV_ERROR");
			}
		};
	} // End of namespace
}
