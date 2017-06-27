/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "obs-precomp.h"   // Precompiled headers


#include <mrpt/utils/CStream.h>
#include <mrpt/obs/CObservationBatteryState.h>
#include <mrpt/system/os.h>

using namespace mrpt::obs;
using namespace mrpt::utils;
using namespace mrpt::poses;
using namespace mrpt::math;

// This must be added to any CSerializable class implementation file.
template <> const char * mrpt::utils::CSerializer<CObservationBatteryState>::getClassName() { return "CObservationBatteryState";}

/** Constructor
 */
CObservationBatteryState::CObservationBatteryState( ) :
	 voltageMainRobotBattery(0),
	 voltageMainRobotComputer(0),
	 voltageMainRobotBatteryIsValid(false),
	 voltageMainRobotComputerIsValid(false),
	 voltageOtherBatteries(),
	 voltageOtherBatteriesValid()
{
}

namespace mrpt {
namespace utils {
/*---------------------------------------------------------------
  Implements the writing to a CStream capability of CSerializable objects
 ---------------------------------------------------------------*/
template <> void  CSerializer<CObservationBatteryState>::writeToStream(const CObservationBatteryState &o, mrpt::utils::CStream &out, int *version)
{
	MRPT_UNUSED_PARAM(out);
	if (version)
		*version = 2;
	else
	{
		// The data
		out << o.voltageMainRobotBattery
			<< o.voltageMainRobotComputer
			<< o.voltageMainRobotBatteryIsValid
			<< o.voltageMainRobotComputerIsValid
			<< o.voltageOtherBatteries
			<< o.voltageOtherBatteriesValid
			<< o.sensorLabel
			<< o.timestamp;
	}
}

/*---------------------------------------------------------------
  Implements the reading from a CStream capability of CSerializable objects
 ---------------------------------------------------------------*/
template <> void CSerializer<CObservationBatteryState>::readFromStream(CObservationBatteryState& o, mrpt::utils::CStream &in, int version)
{
	MRPT_UNUSED_PARAM(in);
	switch(version)
	{
	case 0:
	case 1:
	case 2:
		{
			in	>> o.voltageMainRobotBattery
				>> o.voltageMainRobotComputer
				>> o.voltageMainRobotBatteryIsValid
				>> o.voltageMainRobotComputerIsValid
				>> o.voltageOtherBatteries
				>> o.voltageOtherBatteriesValid;
			if (version>=1)
				in >> o.sensorLabel;
			else o.sensorLabel="";

			if (version>=2)
					in >> o.timestamp;
			else 	o.timestamp = INVALID_TIMESTAMP;
		} break;
	default:
		MRPT_THROW_UNKNOWN_SERIALIZATION_VERSION(version)

	};
}

}
}

// See base class docs
void CObservationBatteryState::getSensorPose( CPose3D &out_sensorPose ) const { 
	out_sensorPose=CPose3D(0,0,0); 
}
		
// See base class docs
void CObservationBatteryState::setSensorPose( const CPose3D &newSensorPose ) {
	MRPT_UNUSED_PARAM(newSensorPose);
}

void CObservationBatteryState::getDescriptionAsText(std::ostream &o) const
{
	CObservation::getDescriptionAsText(o);

	o << format("Measured VoltageMainRobotBattery: %.02fV  isValid= %s \n",
		voltageMainRobotBattery,
		(voltageMainRobotBatteryIsValid == true)? "True":"False" );

	o << format("Measured VoltageMainRobotComputer: %.02fV  isValid= %s \n",
		voltageMainRobotComputer,
		(voltageMainRobotComputerIsValid == true)? "True":"False" );

	o << "VoltageOtherBatteries: \n";
	for(CVectorDouble::Index i=0; i<voltageOtherBatteries.size(); i++)
	{
		o << format("Index: %d --> %.02fV  isValid= %s \n",
		int(i),
		voltageOtherBatteries[i],
		(voltageOtherBatteriesValid[i] == true)? "True":"False" );
	}

}
