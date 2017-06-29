/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "nav-precomp.h" // Precomp header
#include <mrpt/nav/tpspace/CPTG_DiffDrive_CS.h>
#include <mrpt/system/os.h>

using namespace mrpt;
using namespace mrpt::nav;
using namespace std;
using namespace mrpt::system;
using namespace mrpt::utils;

template <> const char * mrpt::utils::CSerializer<CPTG_DiffDrive_CS>::getClassName() { return "CPTG_DiffDrive_CS";}

void CPTG_DiffDrive_CS::loadFromConfigFile(const mrpt::utils::CConfigFileBase &cfg,const std::string &sSection)
{
	CPTG_DiffDrive_CollisionGridBased ::loadFromConfigFile(cfg,sSection);
	
	MRPT_LOAD_CONFIG_VAR_NO_DEFAULT(K,double, cfg,sSection);

	// The constant curvature turning radius used in this PTG:
	R = V_MAX / W_MAX;
}
void CPTG_DiffDrive_CS::saveToConfigFile(mrpt::utils::CConfigFileBase &cfg,const std::string &sSection) const
{
	MRPT_START
	const int WN = 25, WV = 30;
	CPTG_DiffDrive_CollisionGridBased::saveToConfigFile(cfg,sSection);

	cfg.write(sSection,"K",K,   WN,WV, "K=+1 forward paths; K=-1 for backwards paths.");

	MRPT_END
}

namespace mrpt { namespace utils {
template <> void CSerializer<CPTG_DiffDrive_CS>::readFromStream(CPTG_DiffDrive_CS &o, mrpt::utils::CStream &in, int version)
{
	o.internal_readFromStream(in);

	switch (version)
	{
	case 0:
		in >> o.K;
		break;
	default:
		MRPT_THROW_UNKNOWN_SERIALIZATION_VERSION(version)
	};
}

template <> void CSerializer<CPTG_DiffDrive_CS>::writeToStream(const CPTG_DiffDrive_CS &o, mrpt::utils::CStream &out, int *version)
{
	if (version) 
	{
		*version = 0;
		return;
	}

	o.internal_writeToStream(out);
	out << o.K;
}
}}

std::string CPTG_DiffDrive_CS::getDescription() const
{
	char str[100];
	os::sprintf(str,100,"CPTG_DiffDrive_CS,K=%i",(int)K);
	return std::string(str);
}

void CPTG_DiffDrive_CS::ptgDiffDriveSteeringFunction( float alpha, float t,float x, float y, float phi, float &v, float &w ) const
{
	MRPT_UNUSED_PARAM(x); MRPT_UNUSED_PARAM(y); MRPT_UNUSED_PARAM(phi);
	const float T = 0.847f*std::sqrt(std::abs(alpha))*R/V_MAX;

	if (t< T)
	{
		// l+
		v = V_MAX;
		w = W_MAX * min( 1.0f , 1.0f - (float)exp( -square(alpha) ));
	}
	else
	{
		// s+:
		v = V_MAX;
		w = 0;
	}

	// Turn in the opposite direction??
	if (alpha<0)
		w*=-1;

	v*=K;
	w*=K;

}

bool CPTG_DiffDrive_CS::PTG_IsIntoDomain( double x, double y ) const
{
	// If signs of K and X are different, it is not into the domain:
	if ((K*x)<0)
		return false;

	if (fabs(y)>=R)
	{
		// Segmento de arriba:
		return (fabs(x)>R-0.10f);
	}
	else
	{
		// The circle at (0,R):
		return (square(x)+square(fabs(y)-(R+0.10f)))>square(R);
	}
}

void CPTG_DiffDrive_CS::loadDefaultParams()
{
	CPTG_DiffDrive_CollisionGridBased::loadDefaultParams();
	K = +1.0;
}
