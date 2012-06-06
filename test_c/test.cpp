/* test.c: test program for STLS 
   .\test i - test example i, 1 <= i <= 24
   .\test   - test all examples            */ 
#include <limits>
#include <stdio.h>
#include <ctime>
#include <string.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_matrix.h>
#include <gsl/gsl_errno.h>
#include <gsl/gsl_multifit_nlin.h>
#include "slra.h"

int read_mat( gsl_matrix *a, const char * filename ) {
  FILE *file = fopen(filename, "r");
  if (file == NULL) {
    Log::lprintf("Error opening file %s\n", filename);
    return 0;
  }
  gsl_matrix_fscanf(file,a);
  fclose(file);
  return 1;
}

int read_vec( gsl_vector *a, const char * filename ) {
  FILE *file = fopen(filename, "r");
  if (file == NULL) {
    Log::lprintf("Error opening file %s\n", filename);
    return 0;
  }
  gsl_vector_fscanf(file, a);
  fclose(file);
  return 1;
}

#define MAX_FN  60
void run_test( const char * testname, double & time, double& fmin, 
         double &fmin2, int& iter, double& diff, const char* method = "l", 
         int ls_correction = 0, bool silent = false ) {
  gsl_matrix *Rt = NULL, *R = NULL, *v = NULL, *Phi = NULL;
  gsl_vector *p = NULL, *p2 = NULL;
  int rk = 1, s_k, s_q, hasR, hasPhi, hasW, m;
  FILE *file;
  char fnR[MAX_FN], fpname[MAX_FN], fRtname[MAX_FN], fsname[MAX_FN], 
       fRresname[MAX_FN], fpresname[MAX_FN], fnPhi[MAX_FN];
  /* default file names */
  sprintf(fnR, "r%s.txt", testname);
  sprintf(fpname, "p%s.txt", testname);
  sprintf(fRtname, "rt%s.txt", testname);
  sprintf(fsname, "s%s.txt", testname);
  sprintf(fRresname, "res_r%s.txt", testname);
  sprintf(fpresname, "res_p%s.txt", testname);
  sprintf(fnPhi, "phi%s.txt", testname);

  OptimizationOptions opt;
  opt.maxiter = 500;
  
  if (!silent) {
    Log::setMaxLevel(Log::LOG_LEVEL_ITER);
  }
  opt.str2Method(method);
  opt.ls_correction = ls_correction;
  Structure *S = NULL;
  try {
    /* Read structure  and allocate structure object */
    file = fopen(fsname, "r");    
    Log::lprintf("Error opening file %s\n", fsname);
    fscanf(file, "%d %d %d %d %d", &s_k, &s_q, &m, &rk, &hasW); 
    gsl_vector *m_k = gsl_vector_alloc(s_k), *L_q = gsl_vector_alloc(s_q),
               *w_k = gsl_vector_alloc(s_q);
    gsl_vector_fscanf(file, m_k);
    gsl_vector_fscanf(file, L_q);
    if (hasW) {
      gsl_vector_fscanf(file, w_k);
    } else {  
      gsl_vector_set_all(w_k, 1);
    }
    fclose(file);
    S = new MosaicHStructure(L_q, m_k, w_k);
    gsl_vector_free(m_k);  
    gsl_vector_free(w_k);  
    gsl_vector_free(L_q);  
    
    /* Compute invariants and read everything else */ 
    read_vec(p = gsl_vector_alloc(S->getNp()), fpname);
    p2 = gsl_vector_alloc(S->getNp());
    hasR = read_mat(R = gsl_matrix_calloc(m, m - rk), fnR);
    hasPhi = read_mat(Phi = gsl_matrix_alloc(S->getM(), m), fnPhi);
    read_mat(Rt = gsl_matrix_calloc(m, m - rk), fRtname);
    /* call slra */  
    slra(p, S, m-rk, &opt, (hasR ? R : NULL), (hasPhi ? Phi : NULL), NULL,
         p2, R, v);
         
    { 
      Log::lprintf("Test Cholesky factorization speed:\n");
      int rep = 10000;     
      Cholesky *ch = S->createCholesky(m-rk, opt.reggamma);
      clock_t tm = clock();
      
      gsl_matrix *tmpR = gsl_matrix_alloc((hasPhi ? Phi->size1 : R->size1), R->size2);
      
      if (hasPhi) {
        gsl_blas_dgemm(CblasNoTrans, CblasNoTrans, 1.0, Phi, R, 0.0, tmpR);
      } else {
        gsl_matrix_memcpy(tmpR, R);
      }
            
      for (int i = 0; i < rep; i++) {
        ch->calcGammaCholesky(tmpR);
      }        
  
      fmin2 = ((double)clock() - tm) / (rep * CLOCKS_PER_SEC);  
      gsl_matrix_free(tmpR);
      delete ch;
    }
         
    gsl_matrix_fprintf(file = fopen(fRresname,"w"), R, "%.14f");
    fclose(file);
    gsl_vector_fprintf(file = fopen(fpresname, "w"), p2, "%.14f");
    fclose(file);
    gsl_vector_sub(p2, p);
    gsl_matrix_sub(Rt, R);
    gsl_vector Rvec = 
        gsl_vector_view_array(Rt->data, Rt->size1 * Rt->size2).vector;
    double dp_norm = gsl_blas_dnrm2(p2);
    diff = gsl_blas_dnrm2(&Rvec);
    time = opt.time;
    fmin = opt.fmin;
    iter = opt.iter;
//    fmin2 = dp_norm * dp_norm;
    throw 0;
  } catch(...) {
    if (R != NULL) {
      gsl_matrix_free(R);
    }
    if (Rt != NULL) {
      gsl_matrix_free(Rt);
    }
    if (v != NULL) {
      gsl_matrix_free(v);
    }
    if (p != NULL) {
      gsl_vector_free(p);
    }
    if (p2 != NULL) {
      gsl_vector_free(p2);
    }
    if (Phi != NULL) {
      gsl_matrix_free(Phi);
    }
    if (S != NULL) {
      delete S;
    }
    Log::deleteLog();
  }
}

#define TEST_NUM 11

int main(int argc, char *argv[])
{
  double times[TEST_NUM+1], misfits[TEST_NUM+1], misfits2[TEST_NUM+1],  
         diffs[TEST_NUM+1];
  int iters[TEST_NUM+1];
  char num[10];
  const char *method = (argc > 1 ? argv[1] : "l");
  int ls_correction = argc > 2 ? atoi(argv[2]) : 0;
  int i = argc > 3 ? atoi(argv[3]) : -1;

  if (i >= 1) {
    printf("\n------------------ Testing example %d  ------------------\n",i);
    sprintf(num, "%d", i);
    run_test(num, times[i], misfits[i], misfits2[i], 
             iters[i], diffs[i], method, ls_correction);

    printf("\n------------ Results summary --------------------\n\n"
"------------------------------------------------------------------------\n"
"|  # |       Time | Iter |     Minimum | t_chol      |        Diff (R) |\n"
"------------------------------------------------------------------------\n"
"| %2d | %10.6f | %4d | %11.7f | %11.7f | %15.10f |\n"
"------------------------------------------------------------------------\n",
          i, times[i], misfits[i], misfits2[i], iters[i], diffs[i], method);
  } else {
    printf("\n------------------ Testing all examples  ------------------\n");

    for( i = 1; i <= TEST_NUM; i++ ) {
      sprintf(num, "%d", i);
      printf("Running test %s\n", num);  
      run_test(num, times[i], misfits[i], misfits2[i], 
               iters[i], diffs[i], method, ls_correction, false);
      printf("Result: (time, fmin, diff) = %10.8f %10.8f %f\n",
              times[i], misfits[i], diffs[i]);
    }

    printf("\n------------ Results summary --------------------\n\n"
"------------------------------------------------------------------------\n"
"|  # |       Time | Iter |     Minimum | t_chol      |        Diff (X) |\n"
"------------------------------------------------------------------------\n");
    for( i = 1; i <= TEST_NUM; i++ ) {
      printf("| %2d | %10.6f | %4d | %11.7f | %11.7f | %15.10f |\n", 
             i, times[i], misfits[i], misfits2[i], iters[i], diffs[i], method);
    }
    printf(
"------------------------------------------------------------------------\n\n"
           ); 
  }   

  return 0;
}
