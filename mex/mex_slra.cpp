/* MEX function for calling stls.c */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_multifit_nlin.h>
#include "slra.h"

#ifndef BUILD_MEX_OCTAVE
#include "matrix.h"
#endif
#include "mex.h"

/* default constants for the exit condition */

/* field names for opt */
#define EPSABS_STR "epsabs"
#define EPSREL_STR "epsrel"
#define EPSGRAD_STR "epsgrad"
#define MAXITER_STR "maxiter"
#define REGGAMMA_STR "reggamma"
#define DISP_STR "disp"
#define RANK_STR "r"
#define XINI_STR "xini"
#define PERM_STR "phi"
#define WK_STR "w"

#define STR_MAX_LEN 25

/* values for OPT.DISP_STR */
#define NOTIFY_STR "notify"
#define FINAL_STR "final"
#define ITER_STR "iter"
#define OFF_STR "off"

/* field names for s */
#define STR_ARRAY_ML "m"
#define STR_ARRAY_NK "n"

/* field names for info */

#define XH_STR "Xh"
#define VH_STR "Vh"
#define FMIN_STR "fmin"
#define ITER_STR "iter"
#define TIME_STR "time"
#define METHOD_STR "method"


void SLRA_mex_error_handler(const char * reason, const char * file, int line, int gsl_errno) {
  char err_msg[250];

  sprintf(err_msg, "GSL error #%d at %s:%d:  %s", file, line, gsl_errno, reason);
  mexErrMsgTxt(err_msg);
}

void tolowerstr( char * str ) {
  char *c;
  for (c = str; *c != '\0'; c++) {
    *c = tolower(*c);
  }
} 

/* gsl_matrix_const_view MAT_to_trmatview( const mxArray * mat ) {
  return  gsl_matrix_const_view_array(mxGetPr(mat), mxGetN(mat), mxGetM(mat));
} */

gsl_vector_const_view MAT_to_vecview( const mxArray * myMat ) {
  return  gsl_vector_const_view_array(mxGetPr(myMat), mxGetN(myMat) * mxGetM(myMat));
}


gsl_matrix_view MAT_to_trmatview( mxArray * myMat ) {
  return  gsl_matrix_view_array(mxGetPr(myMat), mxGetN(myMat), mxGetM(myMat));
}

gsl_vector_view MAT_to_vecview( mxArray * myMat ) {
  return  gsl_vector_view_array(mxGetPr(myMat), mxGetN(myMat) * mxGetM(myMat));
}

const gsl_vector *view_to_vec( gsl_vector_const_view &vec_vw ) {
  return  vec_vw.vector.data != NULL ? &vec_vw.vector : NULL; 
}

gsl_vector *view_to_vec( gsl_vector_view &vec_vw ) {
  return  vec_vw.vector.data != NULL ? &vec_vw.vector : NULL; 
}

gsl_matrix *view_to_mat( gsl_matrix_view &mat_vw ) {
  return  (mat_vw.matrix.data != NULL) ? &mat_vw.matrix : NULL; 
}


#define IfCheckAndStoreFieldBoundL(name, lvalue)		\
  if (! strcmp(field_name, #name)) {				\
    opt.name = mxGetScalar(mxGetFieldByNumber(prhs[3], 0, l));	\
    if (opt.name < lvalue) {					\
      opt.name = SLRA_DEF_##name;				\
      mexWarnMsgTxt("Ignoring optimization option '"#name"' "	\
		    "because '"#name"' < "#lvalue".");		\
    }								\
  }
                
#define IfCheckAndStoreFieldBoundLU(name, lvalue, uvalue)		\
  if (! strcmp(field_name, #name)) {					\
    opt.name = mxGetScalar(mxGetFieldByNumber(prhs[3], 0, l));		\
    if (opt.name < lvalue || opt.name > uvalue) {			\
      opt.name = SLRA_DEF_##name;					\
      mexWarnMsgTxt("Ignoring optimization option '"#name"' "		\
		    "because '"#name"' < "#lvalue" or '"#name"' > "#uvalue"."); \
    }									\
  }

void mexFunction( int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[] )
{
  gsl_error_handler_t * new_gsl_error_handler = SLRA_mex_error_handler;
  gsl_error_handler_t * old_gsl_error_handler = gsl_set_error_handler(new_gsl_error_handler);
  char str_buf[200];
  gsl_vector_view wk = { { 0, 0, 0, 0, 0 } }; 
  gsl_matrix_view xini = { { 0, 0, 0, 0, 0, 0 } }, perm = { { 0, 0, 0, 0, 0, 0 } }, 
                  xh_view = { { 0, 0, 0, 0, 0, 0 } }, vh_view = { { 0, 0, 0, 0, 0, 0 } };
  
  slraStructure *myStruct;
  int  m, rank;
  opt_and_info opt;
  
  /* Input data */
  if (nrhs < 3) {
    mexErrMsgTxt("Error: at least two parameters (p, s, r) are needed.");
  }


  /* check p */
  gsl_vector_const_view p_in = MAT_to_vecview(prhs[0]);

  /* structure description prhs[1] */
  if (mxGetField(prhs[1], 0, STR_ARRAY_ML) == NULL || 
      mxGetField(prhs[1], 0, STR_ARRAY_NK) == NULL) {
    mexErrMsgTxt("Structure specification must contain fields m and n");   
  }
  gsl_vector_view vec_ml = MAT_to_vecview(mxGetField(prhs[1], 0, STR_ARRAY_ML));
  gsl_vector_view vec_nk = MAT_to_vecview(mxGetField(prhs[1], 0, STR_ARRAY_NK));
  
  rank = mxGetScalar(prhs[2]);
  
  /* user supplied options */
  slraAssignDefOptValues(opt);
  if (nrhs > 3) {
    if (! mxIsStruct(prhs[3])) {
      mexWarnMsgTxt("Ignoring 'opt'. The optimization options "
		    "should be passed in a structure.");
    } else {
      int nfields = mxGetNumberOfFields(prhs[3]);
      const char *field_name_ptr;
      char field_name[30], *c;
      for (int l = 0; l < nfields; l++) {
	field_name_ptr = mxGetFieldNameByNumber(prhs[3], l);
	strcpy(field_name, field_name_ptr);
	tolowerstr(field_name);
	
	/* which option */
	if (!strcmp(field_name, XINI_STR)) {
 	  xini = MAT_to_trmatview(mxGetFieldByNumber(prhs[3], 0, l));
 	} else if (!strcmp(field_name, PERM_STR)) {
 	  perm = MAT_to_trmatview(mxGetFieldByNumber(prhs[3], 0, l));
 	} else if (!strcmp(field_name, WK_STR)) {
 	  wk = MAT_to_vecview(mxGetFieldByNumber(prhs[3], 0, l));
 	} else if (!strcmp(field_name, DISP_STR)) {
 	  mxGetString(mxGetFieldByNumber(prhs[3], 0, l), str_buf, 
		      STR_MAX_LEN); 
	  tolowerstr(str_buf);
	  opt.disp = slraString2Disp(str_buf);  
 	} else if (! strcmp(field_name, METHOD_STR)) {
          mxGetString(mxGetFieldByNumber(prhs[3], 0, l), str_buf, 
               STR_MAX_LEN); 
          tolowerstr(str_buf);
          slraString2Method(str_buf, &opt);
        } else {
          IfCheckAndStoreFieldBoundL(maxiter, 0)  
          else IfCheckAndStoreFieldBoundLU(epsabs, 0, 1) 
            else IfCheckAndStoreFieldBoundLU(epsrel, 0, 1) 
              else IfCheckAndStoreFieldBoundLU(epsgrad, 0, 1) 
                else IfCheckAndStoreFieldBoundLU(epsx, 0, 1) 
                  else IfCheckAndStoreFieldBoundLU(step, 0, 1) 
                    else IfCheckAndStoreFieldBoundLU(tol, 0, 1) 
                      else IfCheckAndStoreFieldBoundL(reggamma, 0) 
                        else { 
                          sprintf(str_buf, "Ignoring unrecognized"
                             " optimization option '%s'.", field_name); 
                          mexWarnMsgTxt(str_buf); 
                        } 
        }
      }
    }
  }
  
  
  myStruct = new slraFlexStructureExt(vec_ml.vector.size, vec_nk.vector.size, 
                     vec_ml.vector.data, vec_nk.vector.data, wk.vector.data);
  m = myStruct->getNplusD();

  if (rank <= 0 || rank >= m) {
    mexErrMsgTxt("Incorrect rank\n");   
  }
    
  /* output info */
  plhs[0] = mxCreateDoubleMatrix(mxGetM(prhs[0]), mxGetN(prhs[0]), mxREAL);
  gsl_vector_view p_out = MAT_to_vecview(plhs[0]);

  if (nlhs > 1) {
    int l = 1;
    const char *field_names[] = { XH_STR, VH_STR, FMIN_STR, ITER_STR, TIME_STR };
    plhs[1] = mxCreateStructArray(1, &l, 5, field_names);
    
    mxArray *xh = mxCreateDoubleMatrix((m - rank), rank, mxREAL);
    mxArray *vh = mxCreateDoubleMatrix((m - rank) * rank, (m - rank) * rank, mxREAL);
    xh_view = MAT_to_trmatview(xh);
    vh_view = MAT_to_trmatview(vh);

    mxSetField(plhs[1], 0, XH_STR, xh);
    mxSetField(plhs[1], 0, VH_STR, vh);
  }

  slra(view_to_vec(p_in), myStruct, rank, &opt, 
       view_to_mat(xini), view_to_mat(perm),
       view_to_vec(p_out), view_to_mat(xh_view), view_to_mat(vh_view));

  mxSetField(plhs[1], 0, FMIN_STR, mxCreateDoubleScalar(opt.fmin));
  mxSetField(plhs[1], 0, ITER_STR, mxCreateDoubleScalar(opt.iter));
  mxSetField(plhs[1], 0, TIME_STR, mxCreateDoubleScalar(opt.time));

  delete myStruct;
  gsl_set_error_handler(old_gsl_error_handler);
}





