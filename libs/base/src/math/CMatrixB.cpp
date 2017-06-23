/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */

#include "base-precomp.h"  // Precompiled headers 

#include <mrpt/math/CMatrixB.h>
#include <mrpt/utils/CStream.h>

using namespace mrpt;
using namespace mrpt::math;
using namespace mrpt::utils;


namespace mrpt
{
namespace utils
{
/*---------------------------------------------------------------
						writeToStream
 ---------------------------------------------------------------*/
template <>
void  CSerializer<CMatrixB>::writeToStream(const CMatrixB &o, mrpt::utils::CStream &out, int *out_Version)
{
	if (out_Version)
		*out_Version = 0;
	else
	{
		out << (uint32_t)sizeof(o.m_Val[0][0]);

		// First, write the number of rows and columns:
		out << (uint32_t)o.m_Rows << (uint32_t)o.m_Cols;

		if (o.m_Rows>0 && o.m_Cols>0)
			for (unsigned int i=0;i<o.m_Rows;i++)
				out.WriteBuffer(o.m_Val[i],sizeof(o.m_Val[0][0])*o.m_Cols);
	}

}

/*---------------------------------------------------------------
						readFromStream
 ---------------------------------------------------------------*/
template <>
void  CSerializer<CMatrixB>::readFromStream(CMatrixB &o, mrpt::utils::CStream &in, int version)
{
	switch(version)
	{
	case 0:
		{
			uint32_t size_bool;
			in >> size_bool;
			if ( size_bool != sizeof(o.m_Val[0][0]) )
				THROW_EXCEPTION("Error: size of 'bool' is different in serialized data!")
	
			uint32_t nRows,nCols;

			// First, write the number of rows and columns:
			in >> nRows >> nCols;

			o.setSize(nRows,nCols);

			if (nRows>0 && nCols>0)
				for (unsigned int i=0;i<nRows;i++)
					in.ReadBuffer(o.m_Val[i],sizeof(o.m_Val[0][0])*o.m_Cols);
		} break;
	default:
		MRPT_THROW_UNKNOWN_SERIALIZATION_VERSION(version)

	};
}
}
}
// Implementation of CMatrixBool
CMatrixBool::CMatrixBool(size_t row, size_t col) : CMatrixTemplate<bool>(row,col) { }
CMatrixBool::CMatrixBool( const CMatrixTemplate<bool> &m ) : CMatrixTemplate<bool>(m)  { }
CMatrixBool & CMatrixBool::operator = (const CMatrixTemplate<bool> & m) { CMatrixTemplate<bool>::operator =(m); return *this; }


