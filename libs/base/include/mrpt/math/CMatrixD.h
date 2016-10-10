/* +---------------------------------------------------------------------------+
   |                     Mobile Robot Programming Toolkit (MRPT)               |
   |                          http://www.mrpt.org/                             |
   |                                                                           |
   | Copyright (c) 2005-2016, Individual contributors, see AUTHORS file        |
   | See: http://www.mrpt.org/Authors - All rights reserved.                   |
   | Released under BSD License. See details in http://www.mrpt.org/License    |
   +---------------------------------------------------------------------------+ */
#ifndef CMATRIXD_H
#define CMATRIXD_H

#include <mrpt/math/CMatrixTemplateNumeric.h>
#include <memory>
namespace mrpt
{
	namespace math
	{


		/**  This class is a "CSerializable" wrapper for "CMatrixTemplateNumeric<double>".
		 * \note For a complete introduction to Matrices and vectors in MRPT, see: http://www.mrpt.org/Matrices_vectors_arrays_and_Linear_Algebra_MRPT_and_Eigen_classes
		 * \ingroup mrpt_base_grp
		 */
		class BASE_IMPEXP_TEMPL CMatrixD : public CMatrixTemplateNumeric<double>
		{
		public:
			typedef std::shared_ptr<CMatrixD> Ptr;
			/** Constructor */
			CMatrixD() : CMatrixTemplateNumeric<double>(1,1)
			{ }

			/** Constructor */
			CMatrixD(size_t row, size_t col) : CMatrixTemplateNumeric<double>(row,col)
			{ }

			/** Copy constructor */
			CMatrixD( const CMatrixTemplateNumeric<double> &m ) : CMatrixTemplateNumeric<double>(m)
			{ }

			/** Copy constructor  */
			CMatrixD( const CMatrixFloat &m ) : CMatrixTemplateNumeric<double>(0,0) {
				*this = m.eval().cast<double>();
			}

			/*! Assignment operator from any other Eigen class */
			template<typename OtherDerived>
			inline CMatrixD & operator= (const Eigen::MatrixBase <OtherDerived>& other) {
				CMatrixTemplateNumeric<double>::operator=(other);
				return *this;
			}
			/*! Constructor from any other Eigen class */
			template<typename OtherDerived>
			inline CMatrixD(const Eigen::MatrixBase <OtherDerived>& other) : CMatrixTemplateNumeric<double>(other) { }

			/** Constructor from a TPose2D, which generates a 3x1 matrix \f$ [x y \phi]^T \f$  */
			explicit CMatrixD( const TPose2D &p);
			/** Constructor from a TPose3D, which generates a 6x1 matrix \f$ [x y z yaw pitch roll]^T \f$  */
			explicit CMatrixD( const TPose3D &p);
			/** Constructor from a TPoint2D, which generates a 2x1 matrix \f$ [x y]^T \f$ */
			explicit CMatrixD( const TPoint2D &p);
			/** Constructor from a TPoint3D, which generates a 3x1 matrix \f$ [x y z]^T \f$ */
			explicit CMatrixD( const TPoint3D &p);

			void  writeToStream(mrpt::utils::CStream &out, int *out_Version) const;
			void  readFromStream(mrpt::utils::CStream &in, int version);
		}; // end of class definition
		
		BASE_IMPEXP ::mrpt::utils::CStream& operator>>(mrpt::utils::CStream& in, CMatrixD::Ptr &pObj);

	} // End of namespace
} // End of namespace

#endif
