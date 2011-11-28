#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_errno.h>

#include <gsl/gsl_blas.h>
#include <gsl/gsl_math.h>

#include "stls.h"


/*
* tmv_prod: block-Toeplitz matrix T times vector v
*
* tt - storage for [t_s-1' ... t_1' t_0 t_1 ... t_s-1].
* s - number of blocks in t, t = [t_0 ... t_s-1]
* s_1 = s - 1;  m = (int) v->size1 / tt->size1
* p - result
*/ 
void tmv_prod_new( gsl_matrix *tt, int s, gsl_vector* v, int m, 
	      gsl_vector* p)
{
  int i, imax, temp, s_1 = s - 1;
  int row_lim = GSL_MIN(s_1, m/2);
  gsl_vector_view subv, subp; 	/* subvector of v and p */

  int TM = tt->size1; 		/* = block size */

  gsl_matrix_view submat, source;


  /* form tt */


/*  PRINTF("s = %d, m = %d, row_lim = %d", s, m, row_lim);*/
  /* construct p = T*v */
  gsl_vector_set_zero(p);

  /* beginning and end parts of the product p */
  for (i = 0; i < row_lim; i++) {
    temp = GSL_MIN(s+i, m)*TM;
    /* beginning part */
    subp = gsl_vector_subvector(p, i*TM, TM);
    subv = gsl_vector_subvector(v, 0, temp);
    submat = gsl_matrix_submatrix
      (tt, 0, (s_1-i)*TM, TM, temp);
    gsl_blas_dgemv(CblasNoTrans, 1.0, &submat.matrix, 
		   &subv.vector, 0.0, &subp.vector);
    /* last part */
    subp = gsl_vector_subvector(p, p->size - (i+1)*TM, TM);
    subv = gsl_vector_subvector(v, v->size - temp, temp);
    submat = gsl_matrix_submatrix(tt, 0, (s+i)*TM -temp, TM, temp);    
    gsl_blas_dgemv(CblasNoTrans, 1.0, &submat.matrix, 
		   &subv.vector, 0.0, &subp.vector);
  }

  /* middle part */
  for (i = s_1, imax = m - s_1 ; i < imax; i++) {
    subp = gsl_vector_subvector(p, i*TM, TM);
    subv = gsl_vector_subvector(v, (i-s_1)*TM, tt->size2);
    gsl_blas_dgemv(CblasNoTrans, 1.0, tt, &subv.vector, 
		   0.0, &subp.vector);
  }
  
}







/*
* tmv_prod: block-Toeplitz matrix T times vector v
*
* t - nonzero part of the first block row of T
* s - number of blocks in t, t = [t_0 ... t_s-1]
* s_1 = s - 1;  m = (int) v->size1 / t->size1
* p - result
* 
*/ 

void tmv_prod(gsl_matrix* t, int s, gsl_vector* v, int m, 
	      gsl_vector* p)
{
  gsl_matrix *tt;
  int i, imax, temp, s_1 = s - 1;
  gsl_vector_view subv, subp; 	/* subvector of v and p */

  int TM = t->size1; 		/* = block size */
  int TN = (t->size2);		/* = s(block size) */


  /* tt - storage for [t_s-1' ... t_1' t_0 t_1 ... t_s-1]. Should be t->size1* (2 * t->size2 - t->size1). */

  gsl_matrix_view submat, source;
  


  /* form tt */
  tt = gsl_matrix_alloc(TM, 2*TN - TM);

  for (i = 0; i < s_1; i++) {
    submat = gsl_matrix_submatrix(tt, 0, i*TM, TM, TM);
    source = gsl_matrix_submatrix(t, 0, (s_1-i)*TM, TM, TM);
    gsl_matrix_transpose_memcpy
      (&submat.matrix, &source.matrix);
  }
  submat = gsl_matrix_submatrix(tt, 0, s_1*TM, TM, TN);
  gsl_matrix_memcpy(&submat.matrix, t);

  tmv_prod_new(tt, s,v, m, p);

  free(tt);
  
}

/* find n_d = n+d = sum_{l=1}^q n_l and w->s */
int get_bandwidth_from_structure( const data_struct* s ) {
  int l, max_nl = 1;

  for (l = 0; l < s->q; l++) {
    if ((s->a[l].type == 'T' || s->a[l].type == 'H')) {
      max_nl = mymax(s->a[l].ncol / s->a[l].nb, max_nl);
    }
  }
  
  return max_nl;
}

/* s2w: finds the covariance matrices w from the data structure */
int s2w(const data_struct* s, w_data* w, int n_d, int blocked )
{
  int k, l, i, offset, imax, sum_nl;
  gsl_matrix *zk;
  gsl_matrix_view wi, zkl;
  char err_msg[70];
  int rep;
  int size_wk;
  
 
  w->s = get_bandwidth_from_structure(s);
  
  if (blocked) {
    rep = s->k;
    size_wk = s->k * n_d;
  } else {
    rep = 1;
    size_wk = n_d;
  }

  w->a = (gsl_matrix**) malloc(w->s * sizeof(gsl_matrix *));
  zk   = gsl_matrix_alloc(n_d, n_d);
  /* construct w */
  for (k = 0; k < w->s; k++) { 
    gsl_matrix_set_zero(zk);
    for (l = sum_nl = 0; l < s->q; sum_nl += s->a[l++].ncol) { 
      zkl = gsl_matrix_submatrix(zk, sum_nl, sum_nl, s->a[l].ncol, s->a[l].ncol); 
      switch (s->a[l].type) {
      case 'T': case 'H':
	offset = s->a[l].nb * k;
	imax   = s->a[l].ncol  - offset;
	for (i = 0; i < imax; i++) {
	  if (s->a[l].type == 'H')
	    gsl_matrix_set(&zkl.matrix, i+offset, i, 1);
	  else
	    gsl_matrix_set(&zkl.matrix, i, i+offset, 1);
	}
	
	break;
      case 'U': 
	if (k == 0) {
	  gsl_matrix_set_identity(&zkl.matrix);
	}
	 
	/* else zik is a zero matrix */
	break;
      case 'E': 
	/* zik is a zero matrix */
	break;
      default:
	sprintf(err_msg, "Unknown structure type %c.",
		s->a[l].type);
	GSL_ERROR(err_msg, GSL_EINVAL);
	break;
      }
    }
    w->a[k] = gsl_matrix_calloc(size_wk, size_wk);
    /* w->a[k] = kron(Ik, zk) */
    for (i = 0; i < rep; i++) {
      /* select the i-th diagonal block in a matrix view */
      wi = gsl_matrix_submatrix( w->a[k], i*n_d, 
				 i*n_d, n_d, n_d );
      gsl_matrix_memcpy(&wi.matrix, zk);
    }
  }
  gsl_matrix_free(zk);

  return GSL_SUCCESS;
}

void allocate_and_prepare_data_reshaped( gsl_matrix* a, gsl_matrix* b, const data_struct* s, 
                                         opt_and_info *opt, stls_opt_data_reshaped *P ) {
  PREPARE_COMMON_PARAMS(a, b, s, opt, P, 1); 
  
  P->m = a->size1;
  P->n = a->size2;
  P->d = b->size2;
 
  P->d_times_s = P->d * P->w.s;
  P->d_times_m_div_k = P->d* (int) P->m / s->k;
  P->d_times_s_minus_1 = P->d_times_s - 1;


  /* Form reshaped a and b matrices */
  P->brg_a =  gsl_matrix_alloc(a->size1, a->size2);
  P->brg_b =  gsl_matrix_alloc(b->size1, b->size2);
  
  gsl_matrix_view src_a = gsl_matrix_view_array(a->data, P->m_div_k, s->k * a->tda);
  gsl_matrix_view src_b = gsl_matrix_view_array(b->data, P->m_div_k, s->k * b->tda);
  gsl_matrix_view src_submat, brg_submat;
  int l;
  for (l = 0; l < s->k; l++) {
    src_submat = gsl_matrix_submatrix(&src_a.matrix, 0, l * a->tda, P->m_div_k, P->n);        
    brg_submat = gsl_matrix_submatrix(P->brg_a, P->m_div_k * l, 0, P->m_div_k, P->n);
    gsl_matrix_memcpy(&brg_submat.matrix, &src_submat.matrix);

    src_submat = gsl_matrix_submatrix(&src_b.matrix, 0, l * b->tda, P->m_div_k, P->d);
    brg_submat = gsl_matrix_submatrix(P->brg_b, P->m_div_k * l, 0, P->m_div_k, P->d);
    gsl_matrix_memcpy(&brg_submat.matrix, &src_submat.matrix);
  }

  /* New CholGam */
  P->bx_ext =  gsl_matrix_alloc(P->n_plus_d, P->d);

  P->rb2 = (double*) malloc(P->m_times_d * P->k_times_d_times_s * sizeof(double));
  P->brg_rb = (double*) malloc(P->m_div_k * P->d * P->d_times_s * sizeof(double));
  


  P->brg_ldwork = 1 + (P->s_minus_1 + 1)* P->d * P->d +  /* pDW */ 
                       3 * P->d + /* 3 * K */
                       mymax(P->s_minus_1 + 1,  P->m_div_k - 1 - P->s_minus_1) * P->d * P->d; /* Space needed for MB02CV */

  P->brg_gamma_vec = (double*) malloc(P->d * P->d_times_s * sizeof(double));
  P->brg_gamma = gsl_matrix_alloc(P->d, P->d_times_s);
  P->brg_dgamma = gsl_matrix_alloc(P->d, P->d_times_s);
  P->brg_tdgamma = gsl_matrix_alloc(P->brg_dgamma->size1, 2*P->brg_dgamma->size2 - P->brg_dgamma->size1);
  
  P->brg_dwork  = (double*) malloc((size_t)P->brg_ldwork * sizeof(double));
  P->brg_tmp   = gsl_matrix_alloc(P->d, P->n_plus_d);

  P->brg_yr = gsl_vector_alloc(P->m_times_d);  
  P->brg_f = gsl_vector_alloc(P->m_times_d);  

 
  P->brg_j1b_vec = calloc(P->d_times_m_div_k * P->d, sizeof(double));
  P->brg_j1b = gsl_matrix_calloc(P->d_times_m_div_k, P->d);
  
  P->brg_st   = gsl_matrix_alloc(P->m_times_d, P->n_times_d);
  P->brg_jres2  = malloc( P->m_times_d * sizeof(double));



/*  PRINTF("%p %p %p", P->brg_rb, P->brg_dwork, P->brg_gamma_vec);*/
}

void free_memory_reshaped( stls_opt_data_reshaped *P ) {
  int k;

  for (k = 0; k < P->w.s; k++) 
    gsl_matrix_free(P->w.a[k]);
  free(P->w.a);
  
  
  gsl_matrix_free(P->brg_a);
  gsl_matrix_free(P->brg_b);

  gsl_matrix_free(P->bx_ext);
  gsl_matrix_free(P->brg_tmp);
  gsl_matrix_free(P->brg_gamma);
  gsl_matrix_free(P->brg_dgamma);
  gsl_matrix_free(P->brg_tdgamma);
  
  free(P->brg_dwork);
  free(P->brg_gamma_vec);
  gsl_vector_free(P->brg_yr);
  gsl_vector_free(P->brg_f);
  free(P->rb2);
  free(P->brg_j1b_vec);
  gsl_matrix_free(P->brg_j1b);


  free(P->brg_jres2);
  gsl_matrix_free(P->brg_st);
}

#define M (P->m)
#define N (P->n)
#define D (P->d)

/*
#define M (P->a->size1)
#define N (P->a->size2)
#define D (P->b->size2)*/
#define S (P->w.s)
#define SIZE_W (P->w.a[0]->size1)



static void compute_reshaped_f( gsl_vector* f, gsl_matrix_const_view x_mat, stls_opt_data_reshaped * P ) {
  gsl_matrix_view f_mat = gsl_matrix_view_vector(f, M, D); 
 
  gsl_matrix_memcpy(&f_mat.matrix, P->brg_b);
  gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.0, 
     P->brg_a, &x_mat.matrix, -1.0, &f_mat.matrix);
}

static void compute_reshaped_c_minus_1_2_f( gsl_vector* f, int trans, stls_opt_data_reshaped * P ) {
  int info;
  int i;

  dtbtrs_("U", (trans ? "T" : "N"), "N", 
    &P->d_times_m_div_k, 
    &P->d_times_s_minus_1, 
    &P->k, 
    P->brg_rb, 
    &P->d_times_s, 
    f->data, 
    &P->d_times_m_div_k, 
    &info);
}

static void compute_reshaped_c_minus_1_f( gsl_vector* f, stls_opt_data_reshaped * P ) {
  int info;
  
    dpbtrs_("U", 
      &P->d_times_m_div_k, 
      &P->d_times_s_minus_1, 
      &P->k, 
      P->brg_rb, 
      &P->d_times_s, 
      f->data, 
      &P->d_times_m_div_k, 
      &info);
  
}

/*************************************
 * This function convert between  representations
 *     D * K * m_div_k  array (original)
 *     D * m_div_k * K  array (reshaped)
 *
 *  forward = 1  :  original -> reshaped
 *  forward = 0  :  reshaped -> original
 * 
 * 
 * Reshaped vector can be multiplied by (smaller) block of reshaped gamma matrix
 *************************************/
/*void reshape_f( gsl_vector *reshaped, gsl_vector * original, void* params, int forward ) {
  gsl_vector_view w_orig, w_resh;
   int j, i; 
    
  for (j = 0; j < P->m_div_k;  j++)  {
    for (i = 0; i < P->k;  i++)  {
      w_orig = gsl_vector_subvector(original, (j*P->k + i) *D, D);
      w_resh = gsl_vector_subvector(reshaped, (j + i * P->m_div_k) *D, D);
       
      if (forward)  {
        gsl_vector_memcpy(&w_resh.vector, &w_orig.vector);
      } else {
        gsl_vector_memcpy(&w_orig.vector, &w_resh.vector);
      }
    }
  }  
}*/



/* ---- Auxiliary functions ---- */

/* 
*  XMAT2XEXT: x_mat |-> x_ext
*
*  x_mat  - the parameter x viewed as a matrix, 
*  x_ext  = kron( I_k, [ x; -I_d ] ),
*  params - used for the dimensions n = rowdim(X), 
*           d = coldim(X), k, and n_plus_d = n + d
*/
void xmat2_block_of_xext( gsl_matrix_const_view x_mat, gsl_matrix *bx_ext )
{
  gsl_vector_view diag;
  gsl_matrix_view submat;
  int n = x_mat.matrix.size1,
      d = x_mat.matrix.size2;

  /* set block (1,1) of x_ext to [ x_mat; -I_d ] */
  submat = gsl_matrix_submatrix(bx_ext, 0, 0, n, d); /* select x in (1,1) */
  gsl_matrix_memcpy(&submat.matrix, &x_mat.matrix); /* assign x */
  submat = gsl_matrix_submatrix(bx_ext, n, 0, d, d); /* select -I in (1,1)*/
  gsl_matrix_set_all(&submat.matrix,0);
  diag   = gsl_matrix_diagonal(&submat.matrix);     /* assign -I */
  gsl_vector_set_all(&diag.vector, -1);
}

/* 
*  CHOLGAM: Choleski factorization of the block of reshaped Gamma  matrix
*
*  params - used for the dimensions n = rowdim(X), 
*           d = coldim(X), k, and n_plus_d = n + d
*
*  params.x_ext  = kron( I_k, [ x; -I_d ] ),
*
*  Output:
*    params.brg_rb     - Cholesky factor in a packed form
*/

void cholesky_of_block_of_reshaped_gamma( stls_opt_data_reshaped* P )
{
  int k, info;
  gsl_matrix_view submat;
  gsl_matrix_view  b_w_k; 
  gsl_matrix *bx_ext = P->bx_ext;
  const int zero = 0;

  /* compute brgamma_k = b_x_ext' * w_k(1:(n+d),1:(n+d)) * b_x_ext */
  for (k = 0; k < S; k++) {
    submat = gsl_matrix_submatrix(P->brg_gamma, 0, k*D, D, D);
    b_w_k = gsl_matrix_submatrix(P->w.a[k], 0, 0, P->n_plus_d, P->n_plus_d);
          
    /* compute tmp = x_ext' * w_k */
    gsl_blas_dgemm(CblasTrans, CblasNoTrans, 1.0, bx_ext, &b_w_k.matrix, 0.0, P->brg_tmp);
    /* compute brgamma_k = tmp * x_ext */
    gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.0,  P->brg_tmp, bx_ext, 0.0, &submat.matrix);
  }

  gsl_matrix_vectorize(P->brg_gamma_vec, P->brg_gamma);

  /* Cholesky factorization of Gamma */
  mb02gd_("R", "N",
    &P->d,   /* block size */
    &P->m_div_k,    /* block_dim(brGAMMA) */
    &P->s_minus_1,   /* non-zero block superdiagonals */
    &zero,     /* previous computations */
    &P->m_div_k,           /* to-be-computed */
    P->brg_gamma_vec, /* non-zero part of first block row of brGAMMA */
    &P->d,        /* row_dim(brg_gamma) */
    P->brg_rb,       /* packed Cholesky factor */
    &P->d_times_s, /* row_dim(rb) */
    P->brg_dwork, &P->brg_ldwork, &info); /**/

  /* check for errors of mb02gd */
  if (info == 1) {
  }
  
  if (info) { 
    PRINTF("Error: info = %d", info); /* TO BE COMPLETED */
  }
}









void jacobian_reshaped( stls_opt_data_reshaped* P, gsl_matrix* deriv )
{
  int i, j, l, k, info;
  const int zero = 0, one = 1;
  gsl_matrix_view submat, mat, source; 
  gsl_vector_view subvec, res_vec;
  gsl_vector_view tmp1_row, tmp1_col;
  gsl_vector_view w_k_row, w_k_col;
  gsl_matrix_view submat1, submat2;
  int ibr, ik, i1, j1, kbr;
  double a_kj;

  /* first term of the Jacobian Gamma^{-1/2} kron(a,I_d) */
  for (j = 0; j < N; j++) {
    for (ik = 0; ik < P->k; ik++) {
      /* Fill right-hand matrix */
      for (ibr = 0; ibr < P->m_div_k; ibr++) {
        for (k = 0; k < D; k++) {
          gsl_matrix_set(P->brg_j1b, k + ibr*D, k, gsl_matrix_get(P->a, ik + ibr * P->k,j)) ;
        }
      }
      gsl_matrix_vectorize(P->brg_j1b_vec, P->brg_j1b);

      dtbtrs_("U", "T", "N", 
        &P->d_times_m_div_k, 
        &P->d_times_s_minus_1, 
        &P->d, 
        P->brg_rb, 
        &P->d_times_s, 
        P->brg_j1b_vec, 
        &P->d_times_m_div_k, 
        &info);

      submat1 = gsl_matrix_submatrix(deriv, 0 + ik * P->d_times_m_div_k, j*D, P->d_times_m_div_k, D); 
      gsl_matrix_vec_inv(&submat1.matrix, P->brg_j1b_vec);
    }
  }

  /* second term (naive implementation) */
  res_vec = gsl_vector_view_array(P->brg_jres2, P->m_times_d);

  gsl_matrix *bx_ext = P->bx_ext;
  for (i = 0; i < N; i++) {
    for (j = 0; j < D; j++) {
      /* form dgamma = d/d x_ij gamma */
      gsl_matrix_set_zero(P->brg_tdgamma);

      for (k = 0; k < S; k++) {
        gsl_matrix_view  b_w_k = gsl_matrix_submatrix(P->w.a[k], 0, 0, P->n_plus_d, P->n_plus_d);
      
        submat = gsl_matrix_submatrix(P->brg_tdgamma, 0, (S-1)*D + k*D, D, D);

        /* compute tmp1 = dx_ext' * w_k * x_ext * /
        /* Iterate over rows of dx_ext' * w_k */
        tmp1_row = gsl_matrix_row (&submat.matrix, j);
        w_k_row = gsl_matrix_row (&b_w_k.matrix, i);
        gsl_blas_dgemv(CblasTrans, 1.0, bx_ext, &w_k_row.vector, 0.0, &tmp1_row.vector); 
        
        /* compute submat = submat  + x_ext' * tmp * dx_ext * /
        /* Iterate over rows of dx_ext' * w_k' */
        tmp1_col = gsl_matrix_column (&submat.matrix, j);
        w_k_col = gsl_matrix_column (&b_w_k.matrix, i);
        gsl_blas_dgemv(CblasTrans, 1.0, bx_ext, &w_k_col.vector, 1.0, &tmp1_col.vector); 
       }

      for (l = 0; l < S-1; l++) {
        submat = gsl_matrix_submatrix(P->brg_tdgamma, 0, l*D, D, D);
        source = gsl_matrix_submatrix(P->brg_tdgamma, 0, (2* S-2-l)*D, D, D);
        gsl_matrix_transpose_memcpy(&submat.matrix, &source.matrix);
      }
       
      /* compute st_ij = DGamma * yr */
      subvec = gsl_matrix_column(P->brg_st, i*D+j);
      for (l = 0; l < P->k; l++) { 
        gsl_vector_view subvec_res = gsl_vector_subvector(&res_vec.vector, l * P->d_times_m_div_k, P->d_times_m_div_k);
        gsl_vector_view subvec_yr = gsl_vector_subvector(P->brg_yr, l * P->d_times_m_div_k, P->d_times_m_div_k);
        tmv_prod_new(P->brg_tdgamma, S, &subvec_yr.vector, P->m_div_k, &subvec_res.vector);  
      }
      
      /* solve st_ij = Gamma^{-1/2}st_ij */
      dtbtrs_("U", "T", "N", 
          &P->d_times_m_div_k, 
          &P->d_times_s_minus_1, 
          &P->k, 
          P->brg_rb, 
          &P->d_times_s, 
          res_vec.vector.data,
          &P->d_times_m_div_k, 
          &info); 

      /* New (nonreshaped) */
      gsl_vector_memcpy(&subvec.vector, &res_vec.vector);
    }
  }

  /* deriv = deriv - 0.5 * st */
  gsl_matrix_scale(P->brg_st, 0.5);
  gsl_matrix_sub(deriv, P->brg_st);
}



/* 
*  STLS_F_: STLS cost function evaluation for QN 
*
*  x      - row-wise vectorized matrix X
*  params - parameters for the optimization
*/

double stls_f_reshaped_ (const gsl_vector* x, void* params)
{
  stls_opt_data_reshaped *P = params;

  double ftf;

  /* Use yr as a temporary variable */
  stls_f_reshaped(x, P, P->brg_yr);
  gsl_blas_ddot(P->brg_yr, P->brg_yr, &ftf);

  return ftf;
}

int stls_f_reshaped (const gsl_vector* x, void* params, gsl_vector* f)
{
  stls_opt_data_reshaped *P = params;
  gsl_matrix_const_view x_mat = gsl_matrix_const_view_vector( x, N, D );

  xmat2_block_of_xext( x_mat, P->bx_ext );
  cholesky_of_block_of_reshaped_gamma(P);
  
  compute_reshaped_f(f, x_mat, P);
  compute_reshaped_c_minus_1_2_f(f, 1, P);

  return GSL_SUCCESS;
}

int stls_df_reshaped (const gsl_vector* x, void* params, gsl_matrix* deriv)
{
  stls_opt_data_reshaped *P = params;
  gsl_matrix_const_view x_mat = gsl_matrix_const_view_vector( x, N, D );

  xmat2_block_of_xext(x_mat, P->bx_ext);
  cholesky_of_block_of_reshaped_gamma(P);
  compute_reshaped_f(P->brg_yr, x_mat, P);
  compute_reshaped_c_minus_1_f(P->brg_yr, P);

  jacobian_reshaped(P, deriv);

  return GSL_SUCCESS;
}

int stls_fdf_reshaped (const gsl_vector* x, void* params, gsl_vector* f, 
        gsl_matrix* deriv)
{
  stls_opt_data_reshaped *P = params;
  gsl_matrix_const_view x_mat = gsl_matrix_const_view_vector( x, N, D );

  xmat2_block_of_xext( x_mat, P->bx_ext );
  cholesky_of_block_of_reshaped_gamma(P);

  compute_reshaped_f(P->brg_yr, x_mat, P);
  compute_reshaped_c_minus_1_2_f(P->brg_yr, 1, P);

  gsl_vector_memcpy(f, P->brg_yr);

  compute_reshaped_c_minus_1_2_f(P->brg_yr, 0, P);
  
  jacobian_reshaped(P, deriv);

  return GSL_SUCCESS;
}

int check_and_adjust_parameters( data_struct *s, flex_struct_add_info *psi ) {
  int l;
  psi->total_cols = 0;
  psi->np_scale = 0;
  psi->np_offset = 0;

  for (l = 0; l < s->q; l++) {
    if (s->a[l].type == 'T' || s->a[l].type == 'H') {
      if (s->a[l].ncol %  s->a[l].nb  != 0) { /* Check number of coolumns */
        return GSL_EINVAL;    
      }
    } else {
      s->a[l].nb = s->a[l].ncol; /* Adjust the parameter */
    }
    
    psi->np_offset += s->k * (s->a[l].ncol - s->a[l].nb);
    psi->np_scale += s->a[l].nb;
    psi->total_cols += s->a[l].ncol;
  }  

  return GSL_SUCCESS;
}

#define GET_L(s,l)      ( s->a[l].ncol / s->a[l].nb)
#define GET_T(s,l,m)    (GET_L(s,l) + (m/s->k) - 1) 
#define GET_PLEN(s,l,m) (GET_T(s,l,m) *(s->k) * s->a[l].nb)

int stls_fill_matrix_from_p( gsl_matrix* c,  data_struct *s, gsl_vector* p) {
  int m = c->size1;
  int sum_np = 0, sum_nl = 0;
  int l,j, L, T, p_len;
  gsl_matrix_view p_matr_chunk, c_chunk, p_matr_chunk_sub, c_chunk_sub;
 
  for (l = 0; l < s->q; l++) {
    L = GET_L(s,l);
    T = GET_T(s,l,m);
    p_matr_chunk = gsl_matrix_view_array(&(p->data[sum_np]), T* s->k, s->a[l].nb);
    c_chunk = gsl_matrix_submatrix(c, 0, sum_nl, m, s->a[l].ncol);
    for (j = 0; j < L; j++) {
      p_matr_chunk_sub = gsl_matrix_submatrix(&p_matr_chunk.matrix, j * s->k, 0, m, s->a[l].nb);
      c_chunk_sub = gsl_matrix_submatrix(&c_chunk.matrix, 0, 
          (s->a[l].type == 'T' ? (L- j -1) * s->a[l].nb : j * s->a[l].nb), m, s->a[l].nb);
      gsl_matrix_memcpy(&c_chunk_sub.matrix, &p_matr_chunk_sub.matrix);
    }
    sum_np += GET_PLEN(s, l,m);
    sum_nl += s->a[l].ncol;
  }

  return GSL_SUCCESS;
}

/* Create correction from x */
int stls_correction_reshaped(gsl_vector* p, data_struct *s, void* params, const gsl_vector* x) {
  stls_opt_data_reshaped *P = (stls_opt_data_reshaped *)params;
  int l, j, k, i, L, T;
  int sum_np = 0, sum_nl = 0, p_len;
  gsl_matrix_view b_xext, p_matr_chunk, p_matr_chunk_sub, brgf_matr, res_sub_matr;
  gsl_vector_view p_chunk_vec, brgf_matr_row, res_sub, b_xext_sub;
  gsl_matrix *xext_rev;
  gsl_vector *res;
  double tmp;

  /* Compute r(X) */
  stls_f_reshaped(x, params, P->brg_f);
  compute_reshaped_c_minus_1_2_f(P->brg_f, 0, P);
  brgf_matr = gsl_matrix_view_vector(P->brg_f, P->m, P->d);
    
  /* Create reversed Xext matrix for Toeplitz blocks */
  xext_rev = gsl_matrix_alloc(P->n_plus_d, P->d); 

  for (i = 0; i < P->n_plus_d; i++) {
    b_xext_sub = gsl_matrix_row(P->bx_ext, P->n_plus_d-i-1);
    gsl_matrix_set_row(xext_rev, i,  &b_xext_sub.vector);
  }

  /* Allocate a vector for intermediate results */  
  res = gsl_vector_alloc(P->n_plus_d);

  for (l = 0; l < s->q; l++) {
    /* Select submatrix being used */
    if (s->a[l].type == 'T') {
      b_xext = gsl_matrix_submatrix(xext_rev, (P->n_plus_d - (sum_nl+s->a[l].ncol)), 0, s->a[l].ncol, P->d);
    } else {
      b_xext = gsl_matrix_submatrix(P->bx_ext, sum_nl, 0, s->a[l].ncol, P->d); 
    }

    /* Calculate dimensions of current parameter chunk */
    L = GET_L(s,l);
    T = GET_T(s,l,P->m);
    p_len = GET_PLEN(s,l,P->m);

    /* Adjust the result size */
    res_sub = gsl_vector_subvector(res, 0, s->a[l].ncol);
    res_sub_matr = gsl_matrix_view_vector(&res_sub.vector, L, s->a[l].nb);
    /* Subtract correction if needed */
    if (s->a[l].type != 'E') {
      p_matr_chunk = gsl_matrix_view_array(&(p->data[sum_np]), T , s->a[l].nb * P->k);
      for (j = 0; j < P->k; j++) {
        for (k = 0; k < P->m_div_k; k++) {
          p_matr_chunk_sub = gsl_matrix_submatrix(&p_matr_chunk.matrix, k, s->a[l].nb * j,  L, s->a[l].nb);
          brgf_matr_row = gsl_matrix_row(&brgf_matr.matrix, k + j * P->m_div_k); 
          gsl_blas_dgemv(CblasNoTrans, 1.0, &b_xext.matrix, &brgf_matr_row.vector, 0.0, &res_sub.vector);
          gsl_matrix_sub(&p_matr_chunk_sub.matrix, &res_sub_matr.matrix); 
        }
      }
    }
    sum_np += p_len;
    sum_nl += s->a[l].ncol;
  }
  
  gsl_vector_free(res);
  gsl_matrix_free(xext_rev);
}

