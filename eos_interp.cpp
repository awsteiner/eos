/*
  -------------------------------------------------------------------
  
  Copyright (C) 2022-2023, Andrew W. Steiner
  
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.

  -------------------------------------------------------------------
*/
#include "eos_nuclei.h"

#ifdef O2SCL_EIGEN
#include <eigen3/Eigen/Dense>
#endif

using namespace std;
using namespace o2scl;
using namespace o2scl_const;
using namespace o2scl_hdf;

int eos_nuclei::interp_point(std::vector<std::string> &sv,
                             bool itive_com) {

  if (sv.size()<6) {
    cerr << "Not enough arguments interp-point." << endl;
    return 1;
  }
  
  double nB_cent=o2scl::function_to_double(sv[1]);
  double Ye_cent=o2scl::function_to_double(sv[2]);
  double T_cent=o2scl::function_to_double(sv[3])/hc_mev_fm;

  if (!loaded) {
    O2SCL_ERR("No EOS loaded in interp_point.",o2scl::exc_einval);
  }
  if (with_leptons==false) {
    O2SCL_ERR("No EOS leptons in interp_point.",o2scl::exc_einval);
  }
  
  size_t inB=0, iYe=0, iT=0;
  inB=vector_lookup(n_nB2,nB_grid2,nB_cent);
  nB_cent=nB_grid2[inB];
  iYe=vector_lookup(n_Ye2,Ye_grid2,Ye_cent);
  Ye_cent=Ye_grid2[iYe];
  iT=vector_lookup(n_T2,T_grid2,T_cent*hc_mev_fm);
  T_cent=T_grid2[iT]/hc_mev_fm;
  cout << "Adjusted to grid, nB, Ye, T[MeV]: " << nB_cent << " "
       << Ye_cent << " " << T_cent*hc_mev_fm << endl;
  cout << "At grid point: " << inB << " " << iYe << " " << iT << endl;

  int window=o2scl::stoi(sv[4]);
  cout << "Using window size: " << window << endl;

  // Create interpolation object
  interpm_krige_eos ike;
  ike.mode=ike.mode_loo_cv_bf;
  ike.full_min=true;
  ike.def_mmin.verbose=1;
  
  /// Load cs2 from a file
  std::string st_o2="";
  st_o2=sv[5];
  hdf_file hff;
  hff.open(st_o2);
  hdf_input(hff,ike.tgp_cs2);
  hff.close();

  for(int dnB=-window;dnB<=window;dnB++) {
    for(int dYe=-window;dYe<=window;dYe++) {
      for(int dT=-window;dT<=window;dT++) {
        vector<size_t> index={inB+dnB,iYe+dYe,iT+dT};
        if (abs(dnB)+abs(dYe)+abs(dT)<=window &&
            inB+dnB>=0 && inB+dnB<n_nB2 &&
            iYe+dYe>=0 && iYe+dYe<n_Ye2 &&
            iT+dT>=0 && iT+dT<n_T2) {
          if (ike.tgp_cs2.get_rank()>=3 &&
              (ike.tgp_cs2.get(index)>1.0 ||
               !std::isfinite(ike.tgp_cs2.get(index)) ||
               ike.tgp_cs2.get(index)<0.0)) {
            ike.fix_list.push_back(index[0]);
            ike.fix_list.push_back(index[1]);
            ike.fix_list.push_back(index[2]);
            //cout << "fix: " << index[0] << " " << index[1] << " "
            //<< index[2] << endl;
          } else {
            ike.calib_list.push_back(index[0]);
            ike.calib_list.push_back(index[1]);
            ike.calib_list.push_back(index[2]);
            //cout << "cal: " << index[0] << " " << index[1] << " "
            //<< index[2] << endl;
          }
        }
      }
    }
  }
  
  cout << "Using " << ike.calib_list.size()/3 << " points to calibrate"
       << endl;
  cout << "  and attempting to fix " << ike.fix_list.size()/3 << " points."
       << endl;
  size_t count=ike.calib_list.size()/3;

  if (ike.fix_list.size()>0) {
    ike.compute_dists();
    if (ike.calib_list.size()/3!=ike.calib_dists.size()) {
      cerr << "Problem x2." << endl;
      exit(-1);
    }
  }
  
  std::vector<double> len_list={2.0,3.0};
  std::vector<double> l10_list={-15,-13,-11,-9};  
  std::vector<std::vector<double>> ptemp;
  ptemp.push_back(len_list);
  ptemp.push_back(len_list);
  ptemp.push_back(len_list);
  ptemp.push_back(l10_list);
  std::vector<std::vector<std::vector<double>>> param_lists;
  param_lists.push_back(ptemp);
  std::cout << "Going to set_covar()." << std::endl;
  vector<mcovar_funct_rbf_noise> mfr(1);
  mfr[0].len.resize(3);
  ike.set_covar(mfr,param_lists);
  
  ike.skip_optim=true;
  ike.set(nB_grid2,Ye_grid2,T_grid2,tg_F,tg_P,tg_S,
          tg_mun,tg_mup,tg_mue,tg_Fint,tg_Sint,neutron.m,proton.m);
  
  double min_qual=1.0e99;
  vector<double> p(4), min_p;
  for(p[0]=8.0;p[0]<80.0;p[0]*=1.4) {
    for(p[1]=8.0;p[1]<80.0;p[1]*=1.4) {
      for(p[2]=8.0;p[2]<80.0;p[2]*=1.4) {
        for(p[3]=-15.0;p[3]<-2.99;p[3]+=1.0) {
          vector_out(cout,p,true);
          (*ike.cf)[0].set_params(p);
          int success;
          double q=ike.qual_fun(0,success);
          cout << "q,min_qual,succes: "
               << q << " " << min_qual << " " << success << endl;
          cout << endl;
          if (success==0 && q<min_qual) {
            min_p=p;
            min_qual=q;
          }
        }
      }
    }
  }
  
  if (min_qual>0.9e99) {
    cout << "All points failed." << endl;
  } else {
    (*ike.cf)[0].set_params(min_p);
    int st;
    cout << "Last run:\n" << endl;
    vector_out(cout,min_p,true);
    double qt=ike.qual_fun(0,st);
    cout << "min_qual,qt,st,min_p: "
         << min_qual << " " << qt << " " << st << " ";
    vector_out(cout,min_p,true);
    cout << "Success." << endl;
  }

  // Use the interpolation results to fix points 
  std::vector<double> out(1);
  for(size_t j=0;j<ike.fix_list.size();j+=3) {

    vector<size_t> index={ike.fix_list[j],ike.fix_list[j+1],
      ike.fix_list[j+2]};
    
    double nB=nB_grid2[inB];
    double Ye=Ye_grid2[iYe];
    double T_MeV=T_grid2[iT];

    // Electron photon contribution in MeV
    double F_eg=tg_F.get(index)-tg_Fint.get(index);
    
    ike.eval(index,out);
    if (ike.interp_Fint==false) {
      cout << "Correcting (1) from " << tg_F.get(index) << " to "
           << out[0] << endl;
      cout << "Correcting (1b) from " << tg_Fint.get(index) << " to "
           << out[0]-F_eg << endl;
      tg_F.get(index)=out[0];
      tg_Fint.get(index)=out[0]-F_eg;
    } else {
      cout << "Correcting (2) from " << tg_Fint.get(index) << " to "
           << out[0] << endl;
      tg_Fint.get(index)=out[0];
      tg_F.get(index)=out[0]+F_eg;
    }
    
  }

  for(size_t j=0;j<ike.calib_list.size();j+=3) {

    vector<size_t> index={ike.calib_list[j],ike.calib_list[j+1],
      ike.calib_list[j+2]};
    
    double nB=nB_grid2[inB];
    double Ye=Ye_grid2[iYe];
    double T_MeV=T_grid2[iT];
    
    double fact=1.0/(1.0+exp(2.0*(ike.calib_dists[j/3]-2.5)));

    // Electron photon contribution in MeV
    double F_eg=tg_F.get(index)-tg_Fint.get(index);
    
    ike.eval(index,out);
    if (ike.interp_Fint==false) {
      double corr=out[0]-tg_F.get(index);
      cout << "Correcting (3) from " << tg_F.get(index) << " to "
           << tg_F.get(index)+fact*corr << " at dist: "
           << ike.calib_dists[j/3] << endl;
      cout << "Correcting (3b) from " << tg_Fint.get(index) << " to "
           << tg_F.get(index)-F_eg << " at dist: "
           << ike.calib_dists[j/3] << endl;
      tg_F.get(index)+=fact*corr;
      tg_Fint.get(index)=tg_F.get(index)-F_eg;
    } else {
      double corr=out[0]-tg_Fint.get(index);
      cout << "Correcting (4) from " << tg_Fint.get(index) << " to "
           << tg_Fint.get(index)+fact*corr << " at dist: "
           << ike.calib_dists[j/3] << endl;
      tg_Fint.get(index)+=fact*corr;
      tg_F.get(index)=out[0]+F_eg;
    }

  }

  derivs_computed=false;
  with_leptons=false;
  
  return 0;
}

double interpm_krige_eos::dist_cf(size_t i_calib, size_t i_fix) {

  // These are size_t's so we have to convert to double before
  // we subtract
  double dist1=((double)calib_list[i_calib*3])-
    ((double)fix_list[i_fix*3]);
  double dist2=((double)calib_list[i_calib*3+1])-
    ((double)fix_list[i_fix*3+1]);
  double dist3=((double)calib_list[i_calib*3+2])-
    ((double)fix_list[i_fix*3+2]);
  
  return sqrt(dist1*dist1+dist2*dist2+dist3*dist3);
}

void interpm_krige_eos::compute_dists() {
  
  size_t calib_count=calib_list.size()/3;
  size_t fix_count=fix_list.size()/3;
  for(size_t i_calib=0;i_calib<calib_count;i_calib++) {
    double dist_min=dist_cf(i_calib,0);
    for(size_t i_fix=1;i_fix<fix_count;i_fix++) {
      double dist=dist_cf(i_calib,i_fix);
      if (dist<dist_min) dist_min=dist;
    }
    calib_dists.push_back(dist_min);
  }
  
  return;
}

void interpm_krige_eos::set(std::vector<double> &nB_grid2,
                            std::vector<double> &Ye_grid2,
                            std::vector<double> &T_grid2,
                            o2scl::tensor_grid<> &tg_F,
                            o2scl::tensor_grid<> &tg_P,
                            o2scl::tensor_grid<> &tg_S,
                            o2scl::tensor_grid<> &tg_mun,
                            o2scl::tensor_grid<> &tg_mup,
                            o2scl::tensor_grid<> &tg_mue,
                            o2scl::tensor_grid<> &tg_Fint,
                            o2scl::tensor_grid<> &tg_Sint,
                            double mn, double mpx) {

  ubmatrix ix(calib_list.size()/3,3);
  ubmatrix iy(1,calib_list.size()/3);

  if (interp_Fint) {
    cout << "ix[0] ix[1] ix[2] Fint" << endl;
  } else {
    cout << "ix[0] ix[1] ix[2] F" << endl;
  }
  for(size_t j=0;j<calib_list.size();j+=3) {
    ix(j/3,0)=calib_list[j];
    ix(j/3,1)=calib_list[j+1];
    ix(j/3,2)=calib_list[j+2];
    vector<size_t> index={calib_list[j],calib_list[j+1],
      calib_list[j+2]};
    if (interp_Fint) {
      iy(0,j/3)=tg_Fint.get(index);
    } else {
      iy(0,j/3)=tg_F.get(index);
    }
    if (true) {
      cout << ix(j/3,0) << " " << ix(j/3,1) << " "
           << ix(j/3,2) << " " << iy(0,j/3) << " ";
      if (fix_list.size()>3) {
        cout << calib_dists[j/3];
      }
      cout << endl;
    }
  }

  // Make a copy because interpm_krige_eos will keep it
  ubmatrix ix2=ix;

  mneut=mn;
  mprot=mpx;
  
  nB_grid=nB_grid2;
  Ye_grid=Ye_grid2;
  T_grid=T_grid2;

  tgp_F=&tg_F;
  tgp_P=&tg_P;
  tgp_S=&tg_S;
  tgp_mun=&tg_mun;
  tgp_mup=&tg_mup;
  tgp_mue=&tg_mue;
  tgp_Fint=&tg_Fint;
  tgp_Sint=&tg_Sint;
    
  size_t n_nB=nB_grid2.size();
  size_t n_Ye=Ye_grid2.size();
  size_t n_T=T_grid2.size();
    
  std::cout << "Going to set_data()." << std::endl;
  verbose=2;
  set_data(3,1,calib_list.size()/3,ix,iy);

  return;
}
  
int interpm_krige_eos::addl_const(size_t iout, double &ret) {

  // First, we need to ensure the interpolator has been
  // setup to be able to use the eval() and deriv()
  // functions
  
  // Select the row of the data matrix
  mat_y_row_t yiout2(this->y,iout);
  
  // Construct the KXX matrix
  size_t size=this->x.size1();
  
  mat_inv_kxx_t KXX(size,size);
  for(size_t irow=0;irow<size;irow++) {
    mat_x_row_t xrow(this->x,irow);
    for(size_t icol=0;icol<size;icol++) {
      mat_x_row_t xcol(this->x,icol);
      if (irow>icol) {
        KXX(irow,icol)=KXX(icol,irow);
      } else {
        KXX(irow,icol)=(*cf)[iout](xrow,xcol);
      }
    }
  }
  
  if (verbose>2) {
    std::cout << "Done creating covariance matrix with size "
              << size << std::endl;
  }
  
  // Perform the matrix inversion and compute the determinant
  
  double lndet;
  
  // Construct the inverse of KXX
  if (verbose>2) {
    std::cout << "Performing matrix inversion with size "
              << size << std::endl;
  }
  this->inv_KXX[iout].resize(size,size);
  int cret=this->mi.invert_det(size,KXX,this->inv_KXX[iout],lndet);
  if (cret!=0) {
    cout << "Return failed inversion." << endl;
    return 3;
  }
  
  lndet=log(lndet);
  
  if (verbose>2) {
    std::cout << "Done performing matrix inversion with size "
              << size << std::endl;
  }
        
  // Inverse covariance matrix times function vector
  this->Kinvf[iout].resize(size);
  o2scl_cblas::dgemv(o2scl_cblas::o2cblas_RowMajor,
                     o2scl_cblas::o2cblas_NoTrans,
                     size,size,1.0,this->inv_KXX[iout],
                     yiout2,0.0,this->Kinvf[iout]);

  // Done setting interpolator
  // ----------
  
  ret=0.0;
  bool compare=true;

  //cout << "i nB Ye T cs2_itp cs2_tab d2FdnB2_itp d2FdnB2_tab "
  //<< "d2FdYe2_itp d2FdYe2_tab "
  //<< "dPdnB_itp dPdnB_tab1 dPdnB_tab2" << endl;
  for(size_t ilist=0;ilist<(calib_list.size()+fix_list.size())/3;
      ilist++) {
      
    std::cout << "i,nB,Ye,T: " << ilist << " ";

    size_t inB, iYe, iT;
    if (ilist<calib_list.size()/3) {
      inB=calib_list[ilist*3];
      iYe=calib_list[ilist*3+1];
      iT=calib_list[ilist*3+2];
    } else {
      inB=fix_list[(ilist-calib_list.size()/3)*3];
      iYe=fix_list[(ilist-calib_list.size()/3)*3+1];
      iT=fix_list[(ilist-calib_list.size()/3)*3+2];
    }
      
    std::vector<size_t> index={((size_t)inB),((size_t)iYe),((size_t)iT)};
        
    double nB=nB_grid[inB];
    double Ye=Ye_grid[iYe];
    double T_MeV=T_grid[iT];

    cout << inB << " " << iYe <<  " " << iT << " "
         << nB << " " << Ye << " " << T_MeV << endl;
    cout << "interp_Fint: " << interp_Fint << endl;
    
    // Derivatives of the physical coordinates with respect to the indices

    double didnB, djdYe, dkdT, dnBdi, dYedj, dTdk;
    double d2idnB2, d2jdYe2, d2kdT2;
    if (inB>0 && inB<nB_grid.size()-1) {
      dnBdi=(nB_grid[inB+1]-nB_grid[inB-1])/2;
      double t2=(nB_grid[inB+1]-nB_grid[inB]);
      double t1=(nB_grid[inB]-nB_grid[inB-1]);
      d2idnB2=(1.0/t2-1.0/t1)/(nB_grid[inB+1]-nB_grid[inB-1])*2.0;
    } else if (inB==0) {
      dnBdi=(nB_grid[1]-nB_grid[0]);
      double t2=(nB_grid[2]-nB_grid[1]);
      double t1=(nB_grid[1]-nB_grid[0]);
      d2idnB2=(1.0/t2-1.0/t1)/(nB_grid[2]-nB_grid[0])*2.0;
    } else {
      dnBdi=(nB_grid[nB_grid.size()-1]-nB_grid[nB_grid.size()-2]);
      double t2=(nB_grid[nB_grid.size()-1]-nB_grid[nB_grid.size()-2]);
      double t1=(nB_grid[nB_grid.size()-2]-nB_grid[nB_grid.size()-3]);
      d2idnB2=(1.0/t2-1.0/t1)/(nB_grid[nB_grid.size()-1]-
                               nB_grid[nB_grid.size()-3])*2.0;
    }
    if (iYe>0 && iYe<Ye_grid.size()-1) {
      dYedj=(Ye_grid[iYe+1]-Ye_grid[iYe-1])/2;
      double t2=(Ye_grid[iYe+1]-Ye_grid[iYe]);
      double t1=(Ye_grid[iYe]-Ye_grid[iYe-1]);
      d2jdYe2=(1.0/t2-1.0/t1)/(Ye_grid[iYe+1]-Ye_grid[iYe-1])*2.0;
    } else if (iYe==0) {
      dYedj=(Ye_grid[1]-Ye_grid[0]);
      double t2=(Ye_grid[2]-Ye_grid[1]);
      double t1=(Ye_grid[1]-Ye_grid[0]);
      d2jdYe2=(1.0/t2-1.0/t1)/(Ye_grid[2]-Ye_grid[0])*2.0;
    } else {
      dYedj=(Ye_grid[Ye_grid.size()-1]-Ye_grid[Ye_grid.size()-1]);
      double t2=(Ye_grid[Ye_grid.size()-1]-Ye_grid[Ye_grid.size()-2]);
      double t1=(Ye_grid[Ye_grid.size()-2]-Ye_grid[Ye_grid.size()-3]);
      d2jdYe2=(1.0/t2-1.0/t1)/(Ye_grid[Ye_grid.size()-1]-
                               Ye_grid[Ye_grid.size()-3])*2.0;
    }
    if (iT>0 && iT<T_grid.size()-1) {
      dTdk=(T_grid[iT+1]-T_grid[iT-1])/2;
      double t2=(T_grid[iT+1]-T_grid[iT]);
      double t1=(T_grid[iT]-T_grid[iT-1]);
      d2kdT2=(1.0/t2-1.0/t1)/(T_grid[iT+1]-T_grid[iT-1])*2.0;
    } else if (iT==0) {
      dTdk=(T_grid[1]-T_grid[0]);
      double t2=(T_grid[2]-T_grid[1]);
      double t1=(T_grid[1]-T_grid[0]);
      d2kdT2=(1.0/t2-1.0/t1)/(T_grid[2]-T_grid[0])*2.0;
    } else {
      dTdk=(T_grid[T_grid.size()-1]-T_grid[T_grid.size()-1]);
      double t2=(T_grid[T_grid.size()-1]-T_grid[T_grid.size()-2]);
      double t1=(T_grid[T_grid.size()-2]-T_grid[T_grid.size()-3]);
      d2kdT2=(1.0/t2-1.0/t1)/(T_grid[T_grid.size()-1]-
                               T_grid[T_grid.size()-3])*2.0;
    }
    didnB=1.0/dnBdi;
    djdYe=1.0/dYedj;
    dkdT=1.0/dTdk;
    
    // Evaluate the free energy and its derivatives analytically
    // using the interpolator, and then remove a factor of hbar c

    std::vector<double> out(1);
    eval(index,out);

    double Seg=tgp_S->get(index)-tgp_Sint->get(index);
    double Feg=(tgp_F->get(index)-tgp_Fint->get(index))/hc_mev_fm;
    
    double Fint, F;

    if (interp_Fint) {
      // Interacting free energy per baryon in units of 1/fm
      Fint=out[0]/hc_mev_fm;
      // Total free energy
      F=Fint+Feg;
    } else {
      // Total free energy
      F=out[0]/hc_mev_fm;
      // Interacting free energy per baryon in units of 1/fm
      Fint=F-Feg;
    }

    // Electron chemical potential in 1/fm
    double mue=tgp_mue->get(index)/hc_mev_fm;

    if (true) {
      //cout << "Here." << endl;
      elep.include_deriv=true;
      elep.include_muons=false;
      elep.include_photons=true;
      elep.e.mu=elep.e.m;
      elep.e.n=Ye*nB;
      //cout << elep.e.n << " " << elep.e.mu << endl;
      elep.pair_density_eq(Ye*nB,T_MeV/hc_mev_fm);
      //cout << elep.e.n << " " << elep.e.mu << " "
      //<< elep.e.ed << " " << elep.ed.dndmu << endl;
      cout << "Feg[elep],Feg[table]: "
           << (elep.th.ed-T_MeV/hc_mev_fm*elep.th.en)/nB << " "
           << Feg << endl;
      cout << "Seg,Se,Sg,Seg[table]: " << elep.th.en/nB << " "
           << elep.e.en/nB << " " 
           << elep.ph.en/nB << " " << Seg << endl;
      cout << "mue,mue[table]: " << elep.e.mu << " "
           << tgp_mue->get(index)/hc_mev_fm << endl;
    }

    double dFint_dnB, dFint_dYe, dFint_dT, dF_dnB, dF_dYe, dF_dT;
    double Fint_nBnB, Fint_nBYe, Fint_YeYe, Fint_nBT, Fint_YeT, Fint_TT;
    double F_nBnB, F_nBYe, F_YeYe, F_nBT, F_YeT, F_TT;

    if (interp_Fint) {
      
      // First derivatives of the free energy
      deriv(index,out,0);
      double dFdi=out[0]/hc_mev_fm;
      dFint_dnB=dFdi*didnB;
      
      deriv(index,out,1);
      double dFdj=out[0]/hc_mev_fm;
      dFint_dYe=dFdj*djdYe;
      
      deriv(index,out,2);
      double dFdk=out[0]/hc_mev_fm;
      dFint_dT=dFdk*dkdT*hc_mev_fm;
      
      // Convert derivatives of Fint to derivatives of F
      dF_dnB=dFint_dnB+Ye*mue/nB-Feg/nB;
      dF_dYe=dFint_dYe+mue;
      dF_dT=dFint_dT-Seg;
      
      deriv2(index,out,0,0);
      double d2Fdi2=out[0]/hc_mev_fm;
      // d2FdnB2 in fm^5
      Fint_nBnB=d2Fdi2*didnB*didnB+dFdi*d2idnB2;
      
      deriv2(index,out,0,1);
      double d2Fdidj=out[0]/hc_mev_fm;
      // d2FdnBdYe in fm^2
      Fint_nBYe=d2Fdidj*didnB*djdYe;
      
      deriv2(index,out,1,1);
      double d2Fdj2=out[0]/hc_mev_fm;
      // d2FdYe2 in fm^{-1}
      Fint_YeYe=d2Fdj2*djdYe*djdYe+dFdj*d2jdYe2;
      
      deriv2(index,out,0,2);
      double d2Fdidk=out[0]/hc_mev_fm;
      Fint_nBT=d2Fdidk*didnB*dkdT*hc_mev_fm;
      
      deriv2(index,out,1,2);
      double d2Fdjdk=out[0]/hc_mev_fm;
      Fint_YeT=d2Fdjdk*djdYe*dkdT*hc_mev_fm;
      
      deriv2(index,out,2,2);
      double d2Fdk2=out[0]/hc_mev_fm;
      Fint_TT=(d2Fdk2*dkdT*dkdT+dFdk*d2kdT2)*hc_mev_fm*hc_mev_fm;
      
      // Convert second derivatives of Fint to second derivatives of F
      F_nBnB=Fint_nBnB+Ye*Ye/nB/elep.ed.dndmu-2.0*Ye*mue/nB/nB+
        2.0*Feg/nB/nB;
      F_nBYe=Fint_nBYe+Ye/elep.ed.dndmu;
      F_YeYe=Fint_YeYe+nB/elep.ed.dndmu;
      F_nBT=Fint_nBT+Ye/nB/elep.ed.dndmu*elep.ed.dndT+Seg/nB;
      F_YeT=Fint_YeT+1.0/elep.ed.dndmu*elep.ed.dndT;
      F_TT=Fint_TT-elep.ed.dsdT/nB;

    } else {

      // First derivatives of the free energy
      deriv(index,out,0);
      double dFdi=out[0]/hc_mev_fm;
      dF_dnB=dFdi*didnB;
      
      deriv(index,out,1);
      double dFdj=out[0]/hc_mev_fm;
      dF_dYe=dFdj*djdYe;
      
      deriv(index,out,2);
      double dFdk=out[0]/hc_mev_fm;
      dF_dT=dFdk*dkdT*hc_mev_fm;
      
      // Convert derivatives of Fint to derivatives of F
      dFint_dnB=dF_dnB-Ye*mue/nB+Feg/nB;
      dFint_dYe=dF_dYe-mue;
      dFint_dT=dF_dT+Seg;
      
      deriv2(index,out,0,0);
      double d2Fdi2=out[0]/hc_mev_fm;
      // d2FdnB2 in fm^5
      F_nBnB=d2Fdi2*didnB*didnB+dFdi*d2idnB2;
      
      deriv2(index,out,0,1);
      double d2Fdidj=out[0]/hc_mev_fm;
      // d2FdnBdYe in fm^2
      F_nBYe=d2Fdidj*didnB*djdYe;
      
      deriv2(index,out,1,1);
      double d2Fdj2=out[0]/hc_mev_fm;
      // d2FdYe2 in fm^{-1}
      F_YeYe=d2Fdj2*djdYe*djdYe+dFdj*d2jdYe2;
      
      deriv2(index,out,0,2);
      double d2Fdidk=out[0]/hc_mev_fm;
      F_nBT=d2Fdidk*didnB*dkdT*hc_mev_fm;
      
      deriv2(index,out,1,2);
      double d2Fdjdk=out[0]/hc_mev_fm;
      F_YeT=d2Fdjdk*djdYe*dkdT*hc_mev_fm;
      
      deriv2(index,out,2,2);
      double d2Fdk2=out[0]/hc_mev_fm;
      F_TT=(d2Fdk2*dkdT*dkdT+dFdk*d2kdT2)*hc_mev_fm*hc_mev_fm;
      
      // Convert second derivatives of Fint to second derivatives of F
      Fint_nBnB=F_nBnB-Ye*Ye/nB/elep.ed.dndmu+2.0*Ye*mue/nB/nB-
        2.0*Feg/nB/nB;
      Fint_nBYe=F_nBYe-Ye/elep.ed.dndmu;
      Fint_YeYe=F_YeYe-nB/elep.ed.dndmu;
      Fint_nBT=F_nBT-Ye/nB/elep.ed.dndmu*elep.ed.dndT-Seg/nB;
      Fint_YeT=F_YeT-1.0/elep.ed.dndmu*elep.ed.dndT;
      Fint_TT=F_TT+elep.ed.dsdT/nB;

    }

    // Use those derivatives to compute the chemical potentials and
    // the entropy density
    double mun=F-Ye*dF_dYe+nB*dF_dnB;
    double mupmue=F+(1.0-Ye)*dF_dYe+nB*dF_dnB;
    double mup=mupmue-mue;
    double en=-nB*dF_dT;
        
    // Compare theose derivatives with the stored values

    if (compare) {
      /*
      std::cout << "Indices: " << index[0] << " " << index[1] << " "
                << index[2] << std::endl;
      std::cout << "Stored  : mun[MeV],mup[MeV],mue[MeV],S: ";
      */
      std::cout << "  mun table [1/fm], mun interp [1/fm]: "
                << tgp_mun->get(index)/hc_mev_fm << " "
                << mun << endl;
      cout << "    " << F << " " << -Ye*dF_dYe << " "
           << nB*dF_dnB << endl;
      std::cout << "  mup table [1/fm], mup interp [1/fm]: "
                << tgp_mup->get(index)/hc_mev_fm << " "
                << mup << endl;
      /*
      std::cout << tgp_mup->get(index) << " ";
      std::cout << tgp_mue->get(index) << " ";
      std::cout << tgp_S->get(index) << std::endl;
      std::cout << "Computed: mun[MeV],mup[MeV],mue[MeV],S: "
                << mun*hc_mev_fm << " " << mup*hc_mev_fm << " "
                << mue*hc_mev_fm << " ";
      std::cout << en/nB << std::endl;
      */
    }

    std::vector<size_t> im1, ip1, jm1, jp1, km1, kp1;
    std::vector<size_t> ip1jp1, ip1kp1, jp1kp1;
    
    if (index[0]>0) im1={index[0]-1,index[1],index[2]};
    else im1={0,0,0};
    if (index[1]>0) jm1={index[0],index[1]-1,index[2]};
    else jm1={0,0,0};
    if (index[2]>0) km1={index[0],index[1],index[2]-1};
    else km1={0,0,0};
    if (index[0]<nB_grid.size()-1) ip1={index[0]+1,index[1],index[2]};
    else ip1={0,0,0};
    if (index[1]<Ye_grid.size()-1) jp1={index[0],index[1]+1,index[2]};
    else jp1={0,0,0};
    if (index[2]<T_grid.size()-1) kp1={index[0],index[1],index[2]+1};
    else kp1={0,0,0};
    if (index[0]<nB_grid.size()-1 && index[1]<Ye_grid.size()-1) {
      ip1jp1={index[0]+1,index[1]+1,index[2]};
    }
    if (index[0]<nB_grid.size()-1 && index[2]<T_grid.size()-1) {
      ip1kp1={index[0]+1,index[1],index[2]+1};
    }
    if (index[1]<Ye_grid.size()-1 && index[2]<T_grid.size()-1) {
      jp1kp1={index[0],index[1]+1,index[2]+1};
    }
        
    else jp1={0,0,0};

    // Now compute the second derivatives and the speed of sound
        
    double f_nnnn=(Ye*Ye*F_YeYe+nB*(2.0*dF_dnB-2.0*Ye*F_nBYe+nB*F_nBnB))/nB;
    double f_nnnp=((Ye-1.0)*Ye*F_YeYe+nB*(2.0*dF_dnB+(1.0-2.0*Ye)*
                                          F_nBYe+nB*F_nBnB))/nB;
    double f_npnp=((Ye-1.0)*(Ye-1.0)*F_YeYe+nB*(2.0*dF_dnB-2.0*(Ye-1.0)*
                                                F_nBYe+nB*F_nBnB))/nB;
    double f_nnT=dF_dT-Ye*F_YeT+nB*F_nBT;
    double f_npT=dF_dT+(Ye-1.0)*F_YeT+nB*F_nBT;
    double f_TT=nB*F_TT;
    
    double den=en*T_MeV/hc_mev_fm+(mun+mneut)*nB*(1.0-Ye)+
      (mup+mprot)*nB*Ye+mue*nB*Ye;
    std::cout << mun << " " << mup << " " << mneut << " " << mprot << std::endl;
    std::cout << "den,sT,mun*nn,mup*np,mue*np: "
              << den << " " << en*T_MeV/hc_mev_fm << " "
              << (mun+mneut)*nB*(1.0-Ye) << " "
              << (mup+mprot)*nB*Ye << " " << mue*nB*Ye << std::endl;
    double nn2=nB*(1.0-Ye);
    double np2=nB*Ye;
    double cs_sq=(nn2*nn2*(f_nnnn-f_nnT*f_nnT/f_TT)+
                  2.0*nn2*np2*(f_nnnp-f_nnT*f_npT/f_TT)+
                  np2*np2*(f_npnp-f_npT*f_npT/f_TT)-
                  2.0*en*(nn2*f_nnT/f_TT+np2*f_npT/f_TT)-en*en/f_TT)/den;
    cout << "f_nnnn, f_nnnp, f_npnp, f_nnT, f_npT, f_TT, den:\n  "
         << f_nnnn << " " << f_nnnp << " " << f_npnp << " "
         << f_nnT << "\n  " << f_npT << " " << f_TT << " "
         << den << endl;

    cout.setf(ios::showpos);
    cout << "en,f_TT,den: " << en << " " << f_TT << " " << den << endl;
    std::cout << "  t1,t2,t3,t4,t5,t6,cs2 (interp,table): " 
              << nn2*nn2*(f_nnnn-f_nnT*f_nnT/f_TT)/den << " "
              << 2.0*nn2*np2*(f_nnnp-f_nnT*f_npT/f_TT)/den << " "
              << np2*np2*(f_npnp-f_npT*f_npT/f_TT)/den << " "
              << 2.0*en*nn2*f_nnT/f_TT/den << " " 
              << 2.0*en*np2*f_npT/f_TT/den << " "
              << -en*en/f_TT/den << " " << cs_sq << " ";
    cout << tgp_cs2.get(index) << endl;
        
    // Also compute dPdnB
    
    cout << "  dF_dnB (interp, table lo, table hi): " << dF_dnB << " "
         << (tgp_F->get(index)-tgp_F->get(im1))/hc_mev_fm/
      (nB_grid[index[0]]-nB_grid[index[0]-1]) << " "
         << (tgp_F->get(ip1)-tgp_F->get(index))/hc_mev_fm/
      (nB_grid[index[0]+1]-nB_grid[index[0]]) << endl;
    cout << "  dF_dYe (interp, table lo, table hi): " << dF_dYe << " "
         << (tgp_F->get(index)-tgp_F->get(jm1))/hc_mev_fm/
      (Ye_grid[index[1]]-Ye_grid[index[1]-1]) << " "
         << (tgp_F->get(jp1)-tgp_F->get(index))/hc_mev_fm/
      (Ye_grid[index[1]+1]-Ye_grid[index[1]]) << endl;
    cout << "  dF_dT (interp, table lo, table hi): " << dF_dT << " "
         << (tgp_F->get(index)-tgp_F->get(km1))/
      (T_grid[index[2]]-T_grid[index[2]-1]) << " "
         << (tgp_F->get(kp1)-tgp_F->get(index))/
      (T_grid[index[2]+1]-T_grid[index[2]]) << endl;
 
    cout << "  dFint_dnB (interp, table lo, table hi): " << dFint_dnB << " "
         << (tgp_Fint->get(index)-tgp_Fint->get(im1))/hc_mev_fm/
      (nB_grid[index[0]]-nB_grid[index[0]-1]) << " "
         << (tgp_Fint->get(ip1)-tgp_Fint->get(index))/hc_mev_fm/
      (nB_grid[index[0]+1]-nB_grid[index[0]]) << endl;
    cout << "  dFint_dYe (interp, table lo, table hi): " << dFint_dYe << " "
         << (tgp_Fint->get(index)-tgp_Fint->get(jm1))/hc_mev_fm/
      (Ye_grid[index[1]]-Ye_grid[index[1]-1]) << " "
         << (tgp_Fint->get(jp1)-tgp_Fint->get(index))/hc_mev_fm/
      (Ye_grid[index[1]+1]-Ye_grid[index[1]]) << endl;
    cout << "  dFint_dT (interp, table lo, table hi): " << dFint_dT << " "
         << (tgp_Fint->get(index)-tgp_Fint->get(km1))/
      (T_grid[index[2]]-T_grid[index[2]-1]) << " "
         << (tgp_Fint->get(kp1)-tgp_Fint->get(index))/
      (T_grid[index[2]+1]-T_grid[index[2]]) << endl;
 
    cout << "\nSecond derivatives of Fint and F:" << endl;
    if (index[0]>0 && index[0]<nB_grid.size()-1) {
      double t1=(tgp_Fint->get(index)-tgp_Fint->get(im1))/hc_mev_fm/
        (nB_grid[index[0]]-nB_grid[index[0]-1]);
      double t2=(tgp_Fint->get(ip1)-tgp_Fint->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      double t3=(t2-t1)*2.0/(nB_grid[index[0]+1]-nB_grid[index[0]-1]);
      cout << "  Fint_nBnB,Fint_nBnB_intp: "
           << Fint_nBnB << " " << t3 << endl;
      t1=(tgp_F->get(index)-tgp_F->get(im1))/hc_mev_fm/
        (nB_grid[index[0]]-nB_grid[index[0]-1]);
      t2=(tgp_F->get(ip1)-tgp_F->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      t3=(t2-t1)*2.0/(nB_grid[index[0]+1]-nB_grid[index[0]-1]);
      cout << "  F_nBnB,F_nBnB_intp: "
           << F_nBnB << " " << t3 << endl;
    }

    if (index[0]>0 && index[0]<nB_grid.size()-1) {
      double t1=(tgp_Fint->get(ip1)-tgp_Fint->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      double t2=(tgp_Fint->get(ip1jp1)-tgp_Fint->get(jp1))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      double t3=(t2-t1)/(Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      cout << "  Fint_nBYe,Fint_nBYe_intp: "
           << Fint_nBYe << " " << t3 << endl;
      t1=(tgp_F->get(ip1)-tgp_F->get(index))/hc_mev_fm/
        (nB_grid[index[0]]-nB_grid[index[0]-1]);
      t2=(tgp_F->get(ip1jp1)-tgp_F->get(jp1))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      t3=(t2-t1)/(Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      cout << "  F_nBYe,F_nBYe_intp: "
           << F_nBYe << " " << t3 << endl;
    }

    if (index[1]>0 && index[1]<Ye_grid.size()-1) {
      double t1=(tgp_Fint->get(index)-tgp_Fint->get(jm1))/hc_mev_fm/
        (Ye_grid[index[1]]-Ye_grid[index[1]-1]);
      double t2=(tgp_Fint->get(jp1)-tgp_Fint->get(index))/hc_mev_fm/
        (Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      double t3=(t2-t1)*2.0/(Ye_grid[index[1]+1]-Ye_grid[index[1]-1]);
      cout << "  Fint_YeYe,Fint_YeYe_intp: " << Fint_YeYe << " " << t3 << endl;
      t1=(tgp_F->get(index)-tgp_F->get(jm1))/hc_mev_fm/
        (Ye_grid[index[1]]-Ye_grid[index[1]-1]);
      t2=(tgp_F->get(jp1)-tgp_F->get(index))/hc_mev_fm/
        (Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      t3=(t2-t1)*2.0/(Ye_grid[index[1]+1]-Ye_grid[index[1]-1]);
      cout << "  F_YeYe,F_YeYe_intp: " << F_YeYe << " " << t3 << endl;
    }
        
    if (index[0]>0 && index[0]<nB_grid.size()-1) {
      double t1=(tgp_Fint->get(ip1)-tgp_Fint->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      double t2=(tgp_Fint->get(ip1kp1)-tgp_Fint->get(kp1))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
     double t3=(t2-t1)*hc_mev_fm/
        (T_grid[index[2]+1]-T_grid[index[2]]);
      cout << "  Fint_nBT,Fint_nBT_intp: "
           << Fint_nBT << " " << t3 << endl;
      t1=(tgp_F->get(ip1)-tgp_F->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      t2=(tgp_F->get(ip1kp1)-tgp_F->get(kp1))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]);
      t3=(t2-t1)*hc_mev_fm/
        (T_grid[index[2]+1]-T_grid[index[2]]);
      cout << "  F_nBT,F_nBT_intp: "
           << F_nBT << " " << t3 << endl;
    }

    if (index[1]>0 && index[1]<Ye_grid.size()-1) {
      double t1=(tgp_Fint->get(jp1)-tgp_Fint->get(index))/hc_mev_fm/
        (Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      double t2=(tgp_Fint->get(jp1kp1)-tgp_Fint->get(kp1))/hc_mev_fm/
        (Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      double t3=(t2-t1)*hc_mev_fm/
        (T_grid[index[2]+1]-T_grid[index[2]]);
      cout << "  Fint_YeT,Fint_YeT_intp: "
           << Fint_YeT << " " << t3 << endl;
      t1=(tgp_F->get(jp1)-tgp_F->get(index))/hc_mev_fm/
        (Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      t2=(tgp_F->get(jp1kp1)-tgp_F->get(kp1))/hc_mev_fm/
        (Ye_grid[index[1]+1]-Ye_grid[index[1]]);
      t3=(t2-t1)*hc_mev_fm/
        (T_grid[index[2]+1]-T_grid[index[2]]);
      cout << "  F_YeT,F_YeT_intp: "
           << F_YeT << " " << t3 << endl;
    }

    if (index[2]>0 && index[2]<T_grid.size()-1) {
      double t1=(tgp_Fint->get(index)-tgp_Fint->get(km1))/
        (T_grid[index[2]]-T_grid[index[2]-1]);
      double t2=(tgp_Fint->get(kp1)-tgp_Fint->get(index))/
        (T_grid[index[2]+1]-T_grid[index[2]]);
      double t3=hc_mev_fm*(t2-t1)*2.0/(T_grid[index[2]+1]-T_grid[index[2]-1]);
      cout << "  Fint_TT,Fint_TT_intp: " << Fint_TT << " " << t3 << endl;
      t1=(tgp_F->get(index)-tgp_F->get(km1))/
        (T_grid[index[2]]-T_grid[index[2]-1]);
      t2=(tgp_F->get(kp1)-tgp_F->get(index))/
        (T_grid[index[2]+1]-T_grid[index[2]]);
      t3=hc_mev_fm*(t2-t1)*2.0/(T_grid[index[2]+1]-T_grid[index[2]-1]);
      cout << "  F_TT,F_TT_intp: " << F_TT << " " << t3 << endl;
    }
    cout << endl;
    
    cout << "  TI1,TI2: " << nB*(1.0-Ye)*mun+(mup+mue)*nB*Ye-
      tgp_P->get(index)/hc_mev_fm << " "
         << F*nB/hc_mev_fm << endl;
      
    //double mun=Fintp-Ye*dF_dYe+nB*dF_dnB;
    //double mup=Fintp+(1.0-Ye)*dF_dYe+nB*dF_dnB-mue;
    //double dmun_dnB=2*dF_dnB-Ye*F_nBYe+nB*F_nBnB;
    double dmun_dnB=f_nnnn*(1.0-Ye)+f_nnnp*Ye;
    //cout << "9: " << 2*dF_dnB << " " << -Ye*F_nBYe << " "
    //<< nB*F_nBnB << endl;
    
    //double dmup_dnB=2.0*dF_dnB+(Ye-1.0)*F_nBYe+nB*F_nBnB;
    double dmupmue_dnB=f_nnnp*(1.0-Ye)+f_npnp*Ye;
    
    if (index[0]>0 && index[0]<nB_grid.size()-1) {
      std::cout << "  dmun_dnB,dmupmue_dnB,dmun_dnB_intp,"
                << "dmupmue_dnB_intp,dmun_dnB_intp2,dmupmue_dnB_intp2:\n  "
                << dmun_dnB << " " << dmupmue_dnB << " ";
      std::cout << (tgp_mun->get(index)-tgp_mun->get(im1))/hc_mev_fm/
        (nB_grid[index[0]]-nB_grid[index[0]-1]) << " ";
      std::cout << (tgp_mup->get(index)+tgp_mue->get(index)-
                    tgp_mup->get(im1)-tgp_mue->get(im1))/hc_mev_fm/
        (nB_grid[index[0]]-nB_grid[index[0]-1]) << " ";
      std::cout << (tgp_mun->get(ip1)-tgp_mun->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]) << " ";
      std::cout << (tgp_mup->get(ip1)+tgp_mue->get(ip1)-
                    tgp_mup->get(index)-tgp_mue->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]) << endl;
    }
    
    //double dmuden_dnB=((1.0-Ye)*mun+nn2*dmun_dnB+
    //Ye*(mup+mue)+np2*dmup_dnB);
    //double dPdnB=dmuden_dnB-dF_dnB*nB-Fintp;
    double dfdnB=mun*(1.0-Ye)+(mup+mue)*Ye;
    double dPdnB=dmun_dnB*nB*(1.0-Ye)+mun*(1.0-Ye)+
      dmupmue_dnB*nB*Ye+(mup+mue)*Ye-dfdnB;
    if (index[0]>0) {
      cout << "  dPdnB (interp, table lo, table hi): " << dPdnB << " ";
      /*
        cout << "dP_dnB: " << endl;
        std::cout << "Computed: " << dPdnB << std::endl;
        std::cout << "From table: "
        << (tgp_P->get(index)-tgp_P->get(im1))/hc_mev_fm/
        (nB_grid[index[0]]-nB_grid[index[0]-1]) << " "
        << (tgp_P->get(ip1)-tgp_P->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]) << std::endl;
      */
      cout << (tgp_P->get(index)-tgp_P->get(im1))/hc_mev_fm/
        (nB_grid[index[0]]-nB_grid[index[0]-1]) << " "
           << (tgp_P->get(ip1)-tgp_P->get(index))/hc_mev_fm/
        (nB_grid[index[0]+1]-nB_grid[index[0]]) << std::endl;
      
    }
    cout.unsetf(ios::showpos);

    if (dPdnB<=0.0) {
      cout << "Return failure for dPdnB<=0.0." << endl;
      cout << endl;
      return 2;
    }

    if (cs_sq>1.0 || cs_sq<0.0 || !std::isfinite(cs_sq)) {
      cout << "Return failure for unphysical cs_sq." << endl;
      cout << endl;
      return 1;
    }
    cout << endl;
      
  }

  cout << "Return success." << endl;
  return 0;
}
