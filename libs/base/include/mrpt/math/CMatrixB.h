/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2017, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */
#ifndef CMATRIXB_H
#define CMATRIXB_H

#include <mrpt/utils/CSerializable.h>
#include <mrpt/math/CMatrixTemplate.h>

namespace mrpt
{
	namespace math
	{
		// This must be added to any CSerializable derived class:

		/**  This class is a "CSerializable" wrapper for "CMatrixBool".
		 * \note For a complete introduction to Matrices and vectors in MRPT, see: http://www.mrpt.org/Matrices_vectors_arrays_and_Linear_Algebra_MRPT_and_Eigen_classes
		 * \ingroup mrpt_base_grp
		 */
		class BASE_IMPEXP CMatrixB : public mrpt::utils::CSerializable, public CMatrixBool
		{
			// This must be added to any CSerializable derived class:
		public:
			using Ptr = std::shared_ptr<CMatrixB>;
			using ConstPtr = std::shared_ptr<const CMatrixB>;
			/** Constructor */
			CMatrixB(size_t row = 1, size_t col = 1) : CMatrixBool(row,col) { }
			/** Copy constructor */
			CMatrixB( const CMatrixBool &m ) : CMatrixBool(m)  { }
			/** Assignment operator for float matrixes */
			CMatrixB & operator = (const CMatrixBool& m) { CMatrixBool::operator =(m); return *this; }
			void writeToStream(mrpt::utils::CStream &out, int *out_Version) const;
			void readFromStream(mrpt::utils::CStream &in, int version);
		}; // end of class definition

	} // End of namespace
} // End of namespace

#endif
