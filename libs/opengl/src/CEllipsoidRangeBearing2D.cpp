/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "opengl-precomp.h"  // Precompiled header

#include <mrpt/opengl/CEllipsoidRangeBearing2D.h>
#include <mrpt/utils/CStream.h>

using namespace mrpt;
using namespace mrpt::opengl;
using namespace mrpt::utils;
using namespace mrpt::math;
using namespace std;

template <> const char * mrpt::utils::CSerializer<CEllipsoidRangeBearing2D>::getClassName() { return "CEllipsoidRangeBearing2D";}

/*---------------------------------------------------------------
							transformFromParameterSpace
  ---------------------------------------------------------------*/
  void CEllipsoidRangeBearing2D::transformFromParameterSpace(
	const std::vector<BASE::array_parameter_t> &in_pts,
	std::vector<BASE::array_point_t> & out_pts) const
{
	MRPT_START

	// (range,bearing) --> (x,y)
	const size_t N = in_pts.size();
	out_pts.resize(N);
	for (size_t i=0;i<N;i++)
	{
		const double range   = in_pts[i][0];
		const double bearing = in_pts[i][1];
		out_pts[i][0] = range * cos(bearing);
		out_pts[i][1] = range * sin(bearing);
	}

	MRPT_END
}

namespace mrpt {
namespace utils {
/*---------------------------------------------------------------
   Implements the writing to a CStream capability of
     CSerializable objects
  ---------------------------------------------------------------*/
template <> void CSerializer<CEllipsoidRangeBearing2D>::writeToStream(const CEllipsoidRangeBearing2D& o, mrpt::utils::CStream &out,int *version)
{
	if (version)
		*version = 0;
	else
	{
		o.writeToStreamRender(out);
		o.thisclass_writeToStream(out);
	}
}

/*---------------------------------------------------------------
	Implements the reading from a CStream capability of
		CSerializable objects
  ---------------------------------------------------------------*/
template<>
void  CSerializer<CEllipsoidRangeBearing2D>::readFromStream(CEllipsoidRangeBearing2D &o, mrpt::utils::CStream &in,int version)
{
	switch(version)
	{
	case 0:
		{
			o.readFromStreamRender(in);
			o.thisclass_readFromStream(in);
		} break;
	default:
		MRPT_THROW_UNKNOWN_SERIALIZATION_VERSION(version)
	};
	o.notifyChange();
}
}
}
