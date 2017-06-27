/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "opengl-precomp.h"  // Precompiled header

#include <mrpt/opengl/CGridPlaneXY.h>
#include <mrpt/utils/CStream.h>

#include "opengl_internals.h"

using namespace mrpt;
using namespace mrpt::opengl;
using namespace mrpt::utils;
using namespace std;

template <> const char * mrpt::utils::CSerializer<CGridPlaneXY>::getClassName() { return "CGridPlaneXY";}

CGridPlaneXY::Ptr CGridPlaneXY::Create(
				float xMin,
				float xMax,
				float yMin,
				float yMax,
				float z,
				float frequency, 
				float lineWidth,
				bool  antiAliasing)
{
	return CGridPlaneXY::Ptr( new CGridPlaneXY(xMin,xMax,yMin,yMax, z, frequency,lineWidth,antiAliasing ) );
}
/** Constructor  */
CGridPlaneXY::CGridPlaneXY(
	float xMin,
	float xMax,
	float yMin,
	float yMax,
	float z,
	float frequency,
	float lineWidth,
	bool  antiAliasing
	) :
	m_xMin(xMin),
	m_xMax(xMax),
	m_yMin(yMin),
	m_yMax(yMax),
	m_plane_z(z),
	m_frequency(frequency),
	m_lineWidth(lineWidth),
	m_antiAliasing(antiAliasing)
{
}

/*---------------------------------------------------------------
					render_dl
  ---------------------------------------------------------------*/
void   CGridPlaneXY::render_dl() const
{
#if MRPT_HAS_OPENGL_GLUT
	ASSERT_(m_frequency>=0)

	// Enable antialiasing:
	if (m_antiAliasing)
	{
		glPushAttrib( GL_COLOR_BUFFER_BIT | GL_LINE_BIT );
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_BLEND);
		glEnable(GL_LINE_SMOOTH);
	}
	glLineWidth(m_lineWidth);

	glDisable(GL_LIGHTING);  // Disable lights when drawing lines
	glBegin(GL_LINES);

	for (float y=m_yMin;y<=m_yMax;y+=m_frequency)
	{
		glVertex3f( m_xMin,y,m_plane_z );
		glVertex3f( m_xMax,y,m_plane_z );
	}

	for (float x=m_xMin;x<=m_xMax;x+=m_frequency)
	{
		glVertex3f( x,m_yMin,m_plane_z );
		glVertex3f( x,m_yMax,m_plane_z );
	}

	glEnd();
	glEnable(GL_LIGHTING);

	// End antialiasing:
	if (m_antiAliasing)
	{
		glPopAttrib();
		checkOpenGLError();
	}
#endif
}

namespace mrpt{
namespace utils{
/*---------------------------------------------------------------
   Implements the writing to a CStream capability of
     CSerializable objects
  ---------------------------------------------------------------*/
template <> void CSerializer<CGridPlaneXY>::writeToStream(const CGridPlaneXY& o, mrpt::utils::CStream &out,int *version)
{

	if (version)
		*version = 1;
	else
	{
		o.writeToStreamRender(out);
		out << o.m_xMin << o.m_xMax;
		out << o.m_yMin << o.m_yMax << o.m_plane_z;
		out << o.m_frequency;
		out << o.m_lineWidth << o.m_antiAliasing; // v1
	}
}

/*---------------------------------------------------------------
	Implements the reading from a CStream capability of
		CSerializable objects
  ---------------------------------------------------------------*/
template <>
void  CSerializer<CGridPlaneXY>::readFromStream(CGridPlaneXY &o, mrpt::utils::CStream &in,int version)
{

	switch(version)
	{
	case 0:
	case 1:
		{
			o.readFromStreamRender(in);
			in >> o.m_xMin >> o.m_xMax;
			in >> o.m_yMin >> o.m_yMax >> o.m_plane_z;
			in >> o.m_frequency;
			if (version>=1)
				in >> o.m_lineWidth >> o.m_antiAliasing;
			else
			{
				o.m_lineWidth=1.0f;
				o.m_antiAliasing=true;
			}

		} break;
	default:
		MRPT_THROW_UNKNOWN_SERIALIZATION_VERSION(version)

	};
	o.notifyChange();
}

}
}

void CGridPlaneXY::getBoundingBox(mrpt::math::TPoint3D &bb_min, mrpt::math::TPoint3D &bb_max) const
{
	bb_min.x = m_xMin;
	bb_min.y = m_yMin;
	bb_min.z = 0;

	bb_max.x = m_xMax;
	bb_max.y = m_yMax;
	bb_max.z = 0;

	// Convert to coordinates of my parent:
	m_pose.composePoint(bb_min, bb_min);
	m_pose.composePoint(bb_max, bb_max);
}
