#pragma once

/*
 * Copyright (c) 2026 Vincent Mayeski / M2 Tech (16425640 Canada Inc.).
 *
 * Licensed under the MIT License. See LICENSE file in the project root.
 */

#include <string>
#include <boost/format.hpp>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>
#include <gsl/gsl_math.h>
#include <gsl/gsl_eigen.h>

#include "oogsl/gvector.hpp"

// see http://www.mathkeisan.com/UsersGuide/e/man.html#BLAS
// see http://www.csse.uwa.edu.au/programming/gsl-1.0/

namespace oogsl
{
	class matrix 
	{
		gsl_matrix*m;
	public:

		std::size_t n1,n2;

    matrix
      transpose() const
    {
      matrix ret(n2,n1);
      for (std::size_t i=0;i<n1;++i)
      {
        for (std::size_t j=0;j<n2;++j)
        {
          ret(j,i,(*this)(i,j));
        }
      }
      return ret;
    }


    matrix(const std::vector<std::vector<double> > &dat)
    {
      auto _n1=dat.size();
      auto _n2=dat[0].size();
      n1=_n1;
      n2=_n2;
      m=gsl_matrix_alloc(n1,n2);
      for (std::size_t i=0;i<n1;++i)
      {
        for (std::size_t j=0;j<n2;++j)
        {
          (*this)(i,j,dat[i][j]);
        }
      }
    }

    matrix // append matrix A to the side of this
      append_on_right(const matrix &A)
    {
      ASSERT(n1==A.n1,"must have same number of rows");
      auto _n2=n2+A.n2;
      matrix ret(n1,_n2);
      // copy this into ret
      for (std::size_t i=0;i<n1;++i)
      {
        for (std::size_t j=0;j<n2;++j)
        {
          ret(i,j,(*this)(i,j));
        }
      }
      // copy A into ret
      for (std::size_t i=0;i<n1;++i)
      {
        for (std::size_t j=0;j<A.n2;++j)
        {
          ret(i,j+n2,A(i,j));
        }
      }
      return ret;
    }

    std::vector<std::vector<double> > to_dvec() const
    {
      std::vector<std::vector<double> > ret;
      for (std::size_t i=0;i<n1;++i)
      {
        ret.push_back(std::vector<double>());
        for (std::size_t j=0;j<n2;++j)
        {
          ret[i].push_back((*this)(i,j));
        }
      }
      return ret;
    }

		matrix(std::size_t _n1,std::size_t _n2,double x=0,bool diagonal=false)
			:n1(_n1),n2(_n2)
		{
			m=gsl_matrix_alloc(n1,n2);
      if (!diagonal)
        gsl_matrix_set_all(m,x);
      else
      {
        gsl_matrix_set_all(m,0);
        for (uint i=0;i<n1;i++)
        {
          (*this)(i,i,x);
        }
      }
		}

    std::size_t rows() const
    {
      return n1;
    }

    std::size_t cols() const
    {
      return n2;
    }

		matrix(gsl_matrix_view&v)
    //:V(0),S(0)
		{
			m=(gsl_matrix*)malloc(sizeof(gsl_matrix));
			*m=v.matrix;
			n1=m->size1;
			n2=m->size2;
		}

    // creates a diagonal matrix
    matrix(const gvector&source)
      :n1(source.n),n2(source.n)
    {
      m=gsl_matrix_alloc(n1,n2);
			gsl_matrix_set_all(m,0);
      for (uint i=0;i<n1;i++)
      {
        (*this)(i,i,source(i));
      }
    }

    matrix(const matrix&source)
      :n1(source.n1),n2(source.n2)
    {
      m=gsl_matrix_alloc(n1,n2);
      for (uint i=0;i<n1;i++)
        for (uint j=0;j<n2;j++)
          (*this)(i,j,source(i,j));            
    }

    // copy selected columns from source matrix
    matrix(const matrix&source,const std::vector<uint>&idx)
      :n1(source.n1),n2(idx.size())
    {
      m=gsl_matrix_alloc(n1,n2);
      for (uint i=0;i<n1;i++)
        for (uint j=0;j<n2;j++)
          (*this)(i,j,source(i,idx[j]));
    }

    // copy single column from source matrix
    matrix(const matrix&source,uint col)
      :n1(source.n1),n2(1)
    {
      m=gsl_matrix_alloc(n1,n2);
      for (uint i=0;i<n1;i++)
        (*this)(i,0,source(i,col));
    }

		~matrix()
		{
			gsl_matrix_free(m);
		}

    matrix
    operator=(const matrix&source)
    {
      if (n1!=source.n1 || n2!=source.n2)
      {
        n1=source.n1;
        n2=source.n2;
        gsl_matrix_free(m);
        m=gsl_matrix_alloc(n1,n2);
      }
      for (uint i=0;i<n1;i++)
        for (uint j=0;j<n2;j++)
          (*this)(i,j,source(i,j));
      return *this;
    }

		operator gsl_matrix*()
		{
			return m;
		}

    operator const gsl_matrix*() const 
		{
			return m;
		}

    gvector col(uint col)
    {
      gvector ret(n1);
      for (uint i=0;i<n1;i++)
        ret(i,(*this)(i,col));
      return ret;
    }

		// get
		double operator()(std::size_t i,std::size_t j) const
		{
			return gsl_matrix_get(m,i,j);
		}

		// set
		void operator()(std::size_t i,std::size_t j,double x)
		{
			gsl_matrix_set(m,i,j,x);
		}

		void operator+=(const gsl_matrix*b)
		{
			gsl_matrix_add(m,b);
		}

		void operator+=(double x)
		{
			gsl_matrix_add_constant(m,x);
		}

		void operator-=(const gsl_matrix*b)
		{
			gsl_matrix_sub(m,b);
		}

		void operator*=(const gsl_matrix*b)
		{
			gsl_matrix_mul_elements(m,b);
		}

		void operator*=(const double x)
		{
			gsl_matrix_scale(m,x);
		}

		void operator/=(const gsl_matrix*b)
		{
			gsl_matrix_div_elements(m,b);
		}

		//  DGEMM  performs one of the matrix-matrix operations
    //   C := alpha*op( A )*op( B ) + beta*C
    //  where  op( X ) is one of
    //   op( X ) = X   or   op( X ) = X',
		void gemm(
			CBLAS_TRANSPOSE_t TransA,
			CBLAS_TRANSPOSE_t TransB,
			double alpha,
			//const gsl_matrix * A,
			const gsl_matrix * B,
			double beta,
			gsl_matrix * C)
		{
			int rc=gsl_blas_dgemm(TransA,TransB,alpha,m,B,beta,C);
			ASSERT(!rc,"rc");
		}

    static
    matrix
    mult(CBLAS_TRANSPOSE_t TransA,
         CBLAS_TRANSPOSE_t TransB,
         const matrix&A,
         const matrix&B
         )
    {
      const std::size_t MA = (TransA == CblasNoTrans) ? A.n1 : A.n2;
      const std::size_t NB = (TransB == CblasNoTrans) ? B.n2 : B.n1;

      matrix C(MA,NB,0);
      int rc=gsl_blas_dgemm(TransA,TransB,1,A,B,1,C);
			ASSERT(!rc,"rc");
      return C;
    }


    matrix
    mult(             CBLAS_TRANSPOSE_t TransA,
                      CBLAS_TRANSPOSE_t TransB,
                      const matrix&B
                      ) const
    {
      const std::size_t MA = (TransA == CblasNoTrans) ? n1 : n2;
      const std::size_t NB = (TransB == CblasNoTrans) ? B.n2 : B.n1;

      matrix C(MA,NB,0);
      int rc=gsl_blas_dgemm(TransA,TransB,1,m,B,0,C);
			ASSERT(!rc,"rc");
      return C;
    }

    gvector
    mult(CBLAS_TRANSPOSE_t TransA,
         const gvector&x,
         double alpha=1.0) const
    {
      const std::size_t MA = (TransA == CblasNoTrans) ? n1 : n2;
      gvector Y(MA,0);
      ASSERT(n2==x.v->size && n2==x.n,"size");
      int rc=gsl_blas_dgemv(TransA,alpha,m,x,0,Y);
      ASSERT(!rc,"rc");
      return Y;
    }

    static
    matrix
    add(const matrix&A,
        const matrix&B)
    {
      ASSERT(A.n1==B.n1,"size");
      ASSERT(A.n2==B.n2,"size");
      matrix ret(A);
      for (uint i=0;i<A.n1;i++)
      {
        for (uint j=0;j<A.n2;j++)
        {
          ret(i,j,A(i,j)+B(i,j));
        }
      }
      return ret;
    }

    static
    matrix
    sub(const matrix&A,
        const matrix&B)
    {
      ASSERT(A.n1==B.n1,"size");
      ASSERT(A.n2==B.n2,"size");
      matrix ret(A);
      for (uint i=0;i<A.n1;i++)
      {
        for (uint j=0;j<A.n2;j++)
        {
          ret(i,j,A(i,j)-B(i,j));
        }
      }
      return ret;
    }

		// DSYMM  performs one of the matrix-matrix operations
    //        C := alpha*A*B + beta*C
    //       or
    //        C := alpha*B*A + beta*C
    //       where alpha and beta are scalars,  A is a symmetric matrix and  B and C
    //       are  m by n matrices.
		void symm(
			CBLAS_SIDE_t Side,
			CBLAS_UPLO_t Uplo,
			double alpha,
			//const gsl_matrix * A,
			const gsl_matrix * B,
			double beta,
			gsl_matrix * C)
		{
			int rc=gsl_blas_dsymm(Side,Uplo,alpha,m,B,beta,C);
			ASSERT(!rc,"rc");
		}

		//DSYRK  performs one of the symmetric rank k operations
    //      C := alpha*A*A' + beta*C
    //   or
    //      C := alpha*A'*A + beta*C,
    //   where  alpha and beta  are scalars, C is an  n by n   symmetric  matrix
    //   and   A   is an  n by k  matrix in the first case and a  k by n  matrix
    //   in the second case.
		void syrk(
			CBLAS_UPLO_t Uplo,
			CBLAS_TRANSPOSE_t Trans,
			double alpha,
			//const gsl_matrix * A,
			double beta,
			gsl_matrix * C)
		{
			int rc=gsl_blas_dsyrk(Uplo,Trans,alpha,m,beta,C);
			ASSERT(!rc,"rc");
		}

		//DGEMV  performs one of the matrix-vector operations
		//       or
		//          y := alpha*A*x + beta*y
		//       or
		//          y := alpha*A'*x + beta*y
		//       where  alpha and beta are scalars, x and y are vectors and A is an m by
		//       n matrix.
		void gemv(
			CBLAS_TRANSPOSE_t TransA,
			double alpha,
			//const gsl_matrix * A,
			const gsl_vector * X,
			double beta,
			gsl_vector * Y)
		{
			int rc=gsl_blas_dgemv(TransA,alpha,m,X,beta,Y);
			ASSERT(!rc,"rc");
		}

    enum {NONE,INVERSE,SQRT,INVSQRT};

    static
    boost::tuple<matrix,matrix,gvector,matrix> // U,V,S,inv
    svd(const matrix&A,int op=NONE,const gsl_vector*b=0,gsl_vector*x=0)
    {
      matrix U(A);
      matrix V(A.n2,A.n2);
      gvector S(A.n2);

			// This function computes the SVD of the M-by-N matrix A using one-sided Jacobi 
			// orthogonalization for M >= N. The Jacobi method can compute singular values 
			// to higher relative accuracy than Golub-Reinsch algorithms (see references for details).
			// gsl_linalg_SV_decomp_jacobi (gsl_matrix * A, gsl_matrix * V, gsl_vector * S)
			//vector work(n2);
			//int rc1=gsl_linalg_SV_decomp(m,*V,*S,work);
      int rc1=gsl_linalg_SV_decomp_jacobi(U,V,S);
			ASSERT(!rc1,"rc");
      if (x)
      {
        // This function solves the system A x = b using the singular value decomposition (U, S, V) 
        // of A which must have been computed previously with gsl_linalg_SV_decomp. 
        int rc2=gsl_linalg_SV_solve(U,V,S,b,x);
        ASSERT(!rc2,"rc");
      }

      matrix ret(A);

      if (op!=NONE)
      {
        gvector S_op(S);
        if (op==INVERSE)
          S_op.inverse();
        else if (op==SQRT)
          S_op.sqrt();
        else if (op==INVSQRT)
        {
          S_op.sqrt();
          S_op.inverse();
        }
        else
          ASSERT(false,"SNGH");
        const auto&tmp=mult(CblasNoTrans,CblasNoTrans,V,matrix(S_op));
        ret=mult(CblasNoTrans,CblasTrans,tmp,U);
        return boost::tuple<matrix,matrix,gvector,matrix>(U,V,S,ret);
      }
      return boost::tuple<matrix,matrix,gvector,matrix>(U,V,S,ret);
    }

    matrix
    inverse() const
    {
      const auto&r=svd(*this,INVERSE);
      return boost::get<3>(r);
    }

    boost::tuple<gvector,matrix>
    eigen_symm() const
    {
      ASSERT(n1==n2,"size");
      gvector eval(n1);
      matrix evec(n1,n1);
      gsl_eigen_symmv_workspace * w = 
        gsl_eigen_symmv_alloc (n1);

      /*

      Function: int gsl_eigen_symmv (gsl_matrix * A, gsl_vector * eval, gsl_matrix * evec, gsl_eigen_symmv_workspace * w)
      This function computes the eigenvalues and eigenvectors of the real symmetric matrix A. Additional workspace of 
      the appropriate size must be provided in w. 
      The diagonal and lower triangular part of A are destroyed during the computation, 
      but the strict upper triangular part is not referenced. The eigenvalues are stored in 
      the vector eval and are unordered. The corresponding eigenvectors are stored in the columns of the matrix evec. 
      For example, the eigenvector in the first column corresponds to the first eigenvalue. 
      The eigenvectors are guaranteed to be mutually orthogonal and normalised to unit magnitude. 

      */

      int rc=gsl_eigen_symmv(m, eval, evec, w);
      ASSERT(!rc,"rc");
      gsl_eigen_symmv_free (w);
      gsl_eigen_symmv_sort (eval, evec, 
                            GSL_EIGEN_SORT_ABS_DESC);
      return boost::tuple<gvector,matrix>(eval,evec);
    }

#ifndef WIN32

    static
    boost::tuple<gvector,matrix>
    eigen_gen_symm(const matrix&A,const matrix&B)
    {
      ASSERT(A.n1==A.n2,"size");
      ASSERT(B.n1==B.n2,"size");
      ASSERT(A.n1==B.n2,"size");
      gvector eval(A.n1);
      matrix evec(A.n1,A.n1);            
      gsl_eigen_gensymmv_workspace * w =
        gsl_eigen_gensymmv_alloc (A.n1);
      matrix A_copy(A);
      matrix B_copy(B);
      int rc=gsl_eigen_gensymmv(A_copy,B_copy,eval,evec,w);
      ASSERT(!rc,"rc");
      gsl_eigen_gensymmv_free (w);
      gsl_eigen_symmv_sort (eval, evec, 
                            GSL_EIGEN_SORT_ABS_DESC);
      return boost::tuple<gvector,matrix>(eval,evec);
    }

#endif

    gvector
    col(uint j) const
    {
      gvector c(n1);
      int rc=gsl_matrix_get_col (c,m,j);
      ASSERT(!rc,"rc");
      return c;
    }

    gvector
    row(uint i) const
    {
      gvector c(n1);
      int rc=gsl_matrix_get_row (c,m,i);
      ASSERT(!rc,"rc");
      return c;
    }
        

		std::string to_string() const 
		{
			std::string ret="";
			for (uint i=0;i<n1;i++)
			{
				for (uint j=0;j<n2;j++)
					ret+=(boost::format("% f")%gsl_matrix_get(m,i,j)).str()+"\t";
				ret+="\n";
			}
			return ret;
		}

    friend std::ostream& operator<<(std::ostream& out,const matrix&m) 
    {
      return out << m.to_string();
    }

	};

}
