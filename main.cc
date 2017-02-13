
#include <iostream>
#include <iomanip>
#include <sstream>
#include <stdio.h>
#include <string>
#include <math.h>

#include <complex.h> // use standard complex type, must come before fftw
#include <fftw3-mpi.h>

#include "parameter_file.h"
#include "kd_alloc.h"
#include "h5_file.h"
#include "log.h"
#include "initialize.h"

#define Re 0
#define Im 1

struct input_parameters {

    int Nx, Ny;
    int nsteps;
    int out_freq;

    double dx, dt;
    double epsx;
    double epsy;
    double beta;
    double gamma;
    double alpha;
    double kappa;
    double change_etap_thresh;
    double mu_el;
    double nu_el;

    double M0_chem_a;
    double M0_chem_b;
    double M0_chem_c;

    double M1_chem_a;
    double M1_chem_b;
    double M1_chem_c;

    double M0_2H_a;
    double M0_2H_b;
    double M0_Tp_a;
    double M0_Tp_b;

    double M1_2H_a;
    double M1_2H_b;
    double M1_Tp_a;
    double M1_Tp_b;

    double M0_norm;
    double M1_norm;
};

fftw_plan planF_eta[3];
fftw_plan planB_lap[3];
fftw_plan planF_s0n2[3][3];

fftw_plan planB_ux;
fftw_plan planB_uy;

fftw_plan plan_strain_xx;
fftw_plan plan_strain_yy;
fftw_plan plan_strain_xy;

void calc_greens_function(double *** G, double ** kxy, ptrdiff_t local_n0, ptrdiff_t local_0_start, ptrdiff_t N1, struct input_parameters ip)
{
    double pi = 3.14159265359;

    int Nx = ip.Nx;
    int Ny = ip.Ny;
    double dx = ip.dx;
    double mu = ip.mu_el;
    double nu = ip.nu_el;
    double radius = 4*pi/(Nx*dx)/(Ny*dx);

    double * kx = new double [local_n0];
    double * ky = new double [N1/2+1];

    double Lx = Nx*dx;
    double Ly = Ny*dx;

    for (int i=0; i<local_n0; i++)
    {
        int local_x = local_0_start + i;
        if ( local_x < Nx/2+1 ) kx[i] = local_x * 2*pi/Lx;
        else kx[i] = (local_x - Nx) * 2*pi/Lx;
    }

    for (int j=0; j<N1/2+1; j++)
        ky[j] = j * 2*pi/Ly;

    for (int i=0; i<local_n0; i++) 
    for (int j=0; j<N1/2+1; j++)
    {
        int ndx = i*(N1/2+1) + j;

        double k2 = kx[i]*kx[i] + ky[j]*ky[j];
        double norm = sqrt(k2);
        double n0, n1;

        if (k2 < radius) {
            n0 = 0;
            n1 = 0;
        } else {
            n0 = kx[i]/norm;
            n1 = ky[j]/norm;
        }

        G[0][0][ndx] = 1.0/mu - (1+nu)*n0*n0/(2*mu);
        G[1][1][ndx] = 1.0/mu - (1+nu)*n1*n1/(2*mu);

        G[0][1][ndx] = -(1+nu)*n0*n1/(2.0*mu);
        G[1][0][ndx] = -(1+nu)*n1*n0/(2.0*mu);

        if (k2 >= radius) {
            G[0][0][ndx] /= k2;
            G[0][1][ndx] /= k2;
            G[1][0][ndx] /= k2;
            G[1][1][ndx] /= k2;
        } else {
            G[0][0][ndx] = 0;
            G[0][1][ndx] = 0;
            G[1][0][ndx] = 0;
            G[1][1][ndx] = 0;
        }

        kxy[0][ndx] = kx[i];
        kxy[1][ndx] = ky[j];
    }

    delete [] kx;
    delete [] ky;
}

void calc_transformation_strains(double **** epsT, struct input_parameters ip)
{
    const int M0 = 0;
    const int M1 = 1;
    const int X = 0;
    const int Y = 1;
    const double Pi = 3.14159265359;

    double e0[2][2][2];
    double thetav[2][3];

    e0[M0][X][X] = (ip.M0_Tp_a - ip.M0_2H_a)/(ip.M0_2H_a*ip.M0_norm*ip.M0_norm);
    e0[M0][Y][Y] = (ip.M0_Tp_b - ip.M0_2H_b)/(ip.M0_2H_b*ip.M0_norm*ip.M0_norm);
    e0[M0][X][Y] = 0.0;
    e0[M0][Y][X] = 0.0;

    e0[M1][X][X] = (ip.M1_Tp_a - ip.M1_2H_a)/(ip.M1_2H_a*ip.M1_norm*ip.M1_norm);
    e0[M1][Y][Y] = (ip.M1_Tp_b - ip.M1_2H_b)/(ip.M1_2H_b*ip.M1_norm*ip.M1_norm);
    e0[M1][X][Y] = 0.0;
    e0[M1][Y][X] = 0.0;

    thetav[M0][0] = 0.0;
    thetav[M0][1] = 2.0*Pi/3.0;
    thetav[M0][2] = -2.0*Pi/3.0;;

    thetav[M1][0] = thetav[M0][0];
    thetav[M1][1] = thetav[M0][1];
    thetav[M1][2] = thetav[M0][2];

    for (int mat=M0; mat<=M1; mat++)
    for (int pp=0; pp<3; pp++)
    {
        double r[2][2];

        r[0][0] =  cos(thetav[mat][pp]);
        r[1][1] =  cos(thetav[mat][pp]);
        r[0][1] = -sin(thetav[mat][pp]);
        r[1][0] =  sin(thetav[mat][pp]);

        for (int mm=0; mm<2; mm++)
        for (int nn=0; nn<2; nn++)
        {
            epsT[mat][pp][mm][nn] = 0;

            for (int ii=0; ii<2; ii++)
            for (int jj=0; jj<2; jj++)
                epsT[mat][pp][mm][nn] += r[mm][ii]*r[nn][jj]*e0[mat][ii][jj];
        }
    }
}

void calc_elastic_tensors(double **** lam, double **** eps0, double **** sig0, double *** sigeps, double mu, double nu, ptrdiff_t local_n0, ptrdiff_t N1)
{
    const int N1r = 2*(N1/2+1);

    double E = 2*mu*(1+nu);
    lam[0][0][0][0] = E/(1-nu*nu);
    lam[0][0][1][1] = E*nu/(1-nu*nu);
    lam[0][0][1][0] = 0.0;
    lam[0][0][0][1] = 0.0;
    lam[1][1][0][0] = E*nu/(1-nu*nu);
    lam[1][1][1][1] = E/(1-nu*nu);
    lam[1][1][0][1] = 0;
    lam[1][1][1][0] = 0;
    lam[0][1][0][0] = 0;
    lam[0][1][1][1] = 0;
    lam[0][1][0][1] = mu;
    lam[0][1][1][0] = mu;
    lam[1][0][0][0] = 0;
    lam[1][0][1][1] = 0;
    lam[1][0][0][1] = mu;
    lam[1][0][1][0] = mu;

    for (int pp=0; pp<3; pp++)
    {
        for (int i=0; i<local_n0; i++)
        for (int j=0; j<N1; j++)
        {
            int ndx = i*N1r + j;

            for (int ii=0; ii<2; ii++)
            for (int jj=0; jj<2; jj++)
            {
                sig0[pp][ii][jj][ndx] = 0.0;

                for (int kk=0; kk<2; kk++)
                for (int ll=0; ll<2; ll++)
                    sig0[pp][ii][jj][ndx] += lam[ii][jj][kk][ll] * eps0[pp][kk][ll][ndx];
            }
        }
    }

    for (int p=0; p<3; p++)
    for (int q=0; q<3; q++)
    {

        for (int i=0; i<local_n0; i++)
        for (int j=0; j<N1; j++)
        {
            int ndx = i*N1r + j;
            sigeps[p][q][ndx] = 0;

            for (int ii=0; ii<2; ii++)
            for (int jj=0; jj<2; jj++)
                sigeps[p][q][ndx] += sig0[p][ii][jj][ndx]*eps0[q][ii][jj][ndx];
        }
    }
}

void normalize(double * array, ptrdiff_t N0, ptrdiff_t N1, ptrdiff_t local_n0)
{
    const int N1r = 2*(N1/2+1);
    const double area = (double) (N0*N1);
    for (ptrdiff_t i=0; i<local_n0; i++)
    for (ptrdiff_t j=0; j<N1; j++)
    {
        int ndx = i*N1r + j;
        array[ndx] /= area;
    }
}


void introduce_noise(double ** eta, ptrdiff_t local_n0, ptrdiff_t N1)
{
    const int N1r = 2*(N1/2+1);
    for (int i=0; i<local_n0; i++)
    for (int j=0; j<N1; j++)
    {
        int ndx = i*N1r + j;
        double r;
        r = 2*((float)rand())/((float)RAND_MAX) - 1;
        eta[0][ndx] += 0.003*r;
        r = 2*((float)rand())/((float)RAND_MAX) - 1;
        eta[1][ndx] += 0.003*r;
        r = 2*((float)rand())/((float)RAND_MAX) - 1;
        eta[2][ndx] += 0.003*r;
    }
}

void calc_chemical_potential(double ** chem, double ** eta, double *** sigeps, 
                             double ** epsbar, double **** sig0, double *** eps, 
                             double ** lap, double * phi, ptrdiff_t local_n0, ptrdiff_t N1, struct input_parameters ip)
{

    double h_film = pow(6*ip.kappa*(1-ip.nu_el)/ip.mu_el, 1.0/3.0);
    double EelAppl[3] = {0, 0, 0};
    const int N1r = 2*(N1/2+1);

    for (int i=0; i<local_n0; i++)
    for (int j=0; j<N1; j++)
    {
        int ndx = i*N1r + j;

        double f_bulk[3]    = {0,0,0};
        double f_squeeze[3] = {0,0,0};
        double f_homo[3]    = {0,0,0};
        double f_hetero[3]  = {0,0,0};

        double eta_sum = eta[0][ndx]*eta[0][ndx] + eta[1][ndx]*eta[1][ndx] + eta[2][ndx]*eta[2][ndx];


        // bulk free energy
        for (int p=0; p<3; p++)
        {
            double eta_sq = eta[p][ndx]*eta[p][ndx];
            double a = (1-phi[ndx])*ip.M0_chem_a + phi[ndx]*ip.M1_chem_a;
            double b = (1-phi[ndx])*ip.M0_chem_b + phi[ndx]*ip.M1_chem_b;
            double c = (1-phi[ndx])*ip.M0_chem_c + phi[ndx]*ip.M1_chem_c;

            f_bulk[p] = eta[p][ndx]*(a - b*eta_sq + c*eta_sum*eta_sum);
        }


        // stress-free strain (squeeze) part of free energy (double sum)
        for (int p=0; p<3; p++)
        for (int q=0; q<3; q++)
            f_squeeze[p] += 2*sigeps[p][q][ndx]*eta[p][ndx]*eta[q][ndx]*eta[q][ndx];

        // homogenous, macroscropic strain part of free energy
        for (int p=0; p<3; p++)
        {
            EelAppl[p] = -2*( sig0[p][0][0][ndx]*epsbar[0][0]
                            + sig0[p][1][1][ndx]*epsbar[1][1]
                            + sig0[p][0][1][ndx]*epsbar[0][1]
                            + sig0[p][1][0][ndx]*epsbar[1][0] );
            f_homo[p] = EelAppl[p]*eta[p][ndx];
        }

        // heterogenous, local strain part of free energy
        for (int p=0; p<3; p++)
            f_hetero[p] = -2*eta[p][ndx]* ( sig0[p][0][0][ndx]*eps[0][0][ndx]
                                          + sig0[p][0][1][ndx]*eps[0][1][ndx]
                                          + sig0[p][1][0][ndx]*eps[1][0][ndx]
                                          + sig0[p][1][1][ndx]*eps[1][1][ndx] );

        for (int p=0; p<3; p++)
        {
            chem[p][ndx]  = f_bulk[p]; 
            chem[p][ndx] -= ip.beta*lap[p][ndx];
            chem[p][ndx] += f_squeeze[p] + f_homo[p] + f_hetero[p];
        }
    }
}


void calc_uxy(double * ux, double * uy, fftw_complex ** ku, double *** G, double ** kxy, fftw_complex *** ks0n2, ptrdiff_t N0, ptrdiff_t N1, ptrdiff_t local_n0)
{
    const int N1c = N1/2+1;
    for (int i=0; i<local_n0; i++)
    for (int j=0; j<N1c; j++)
    {
        int ndx = i*N1c + j;

        for (int ii=0; ii<2; ii++)
        {
            ku[ii][ndx][Re] = 0;
            ku[ii][ndx][Im] = 0;

            for (int pp=0; pp<3; pp++)
            for (int jj=0; jj<2; jj++)
            for (int kk=0; kk<2; kk++)
            {
                ku[ii][ndx][Re] += G[ii][jj][ndx]*kxy[kk][ndx]*ks0n2[pp][jj+kk][ndx][Im];
                ku[ii][ndx][Im] -= G[ii][jj][ndx]*kxy[kk][ndx]*ks0n2[pp][jj+kk][ndx][Re];
            }
        }
    }

    // ku -> (ux, uy)
    fftw_execute(planB_ux);
    fftw_execute(planB_uy);

    normalize(ux, N0, N1, local_n0);
    normalize(uy, N0, N1, local_n0);
}

double update_eta(double ** eta, double ** eta_old, double ** eta_new, double ** chem, ptrdiff_t local_n0, ptrdiff_t N1, struct input_parameters ip)
{
    double dtg = 0.5*ip.dt*ip.gamma;
    double dtg2 = 1.0/(1.0+dtg);
    double dta2 = ip.dt*ip.dt*ip.alpha*ip.alpha;
    double change_etap_max = 0;
    const int N1r = 2*(N1/2+1);

    for (int p=0; p<3; p++)
    for (int i=0; i<local_n0; i++)
    for (int j=0; j<N1; j++)
    {
        int ndx = i*N1r + j;

        eta_new[p][ndx] = dtg2*(2*eta[p][ndx] + (dtg-1)*eta_old[p][ndx] - dta2*chem[p][ndx]);

        double delta = fabs( eta_new[p][ndx] - eta[p][ndx] );
        change_etap_max = std::max(change_etap_max, delta);

        eta_old[p][ndx] = eta[p][ndx];
        eta[p][ndx] = eta_new[p][ndx];
    }

    return change_etap_max;
}

void calc_ks0n2(double *** s0n2, double **** sig0, double ** eta, ptrdiff_t local_n0, ptrdiff_t N1)
{
    const int X = 0;
    const int Y = 1;

    const int XX = 0;
    const int XY = 1;
    const int YY = 2;

    const int N1r = 2*(N1/2+1);

    for (int p=0; p<3; p++)
    for (int i=0; i<local_n0; i++)
    for (int j=0; j<N1; j++)
    {
        int ndx = i*N1r + j;
        double eta_sq = eta[p][ndx] * eta[p][ndx];
        s0n2[p][XX][ndx] = sig0[p][X][X][ndx] * eta_sq;
        s0n2[p][XY][ndx] = sig0[p][X][Y][ndx] * eta_sq;
        s0n2[p][YY][ndx] = sig0[p][Y][Y][ndx] * eta_sq;
    }

    // s0n2 -> ks0n2
    for (int p=0; p<3; p++)
    for (int i=0; i<3; i++)
        fftw_execute(planF_s0n2[p][i]);
}

void calc_eps(double *** eps, fftw_complex ** keps, double ** kxy, fftw_complex ** ku, ptrdiff_t N0, ptrdiff_t N1, ptrdiff_t local_n0)
{
    const int X = 0;
    const int Y = 1;

    const int XX = 0;
    const int YY = 1;
    const int XY = 2;

    for (int i=0; i<local_n0; i++)
    for (int j=0; j<(N1/2+1); j++)
    {
        int ndx = i*(N1/2+1) + j;
        keps[XX][ndx][Re] = -kxy[X][ndx]*ku[X][ndx][Im];
        keps[XX][ndx][Im] =  kxy[X][ndx]*ku[X][ndx][Re];

        keps[YY][ndx][Re] = -kxy[Y][ndx]*ku[Y][ndx][Im];
        keps[YY][ndx][Im] =  kxy[Y][ndx]*ku[Y][ndx][Re];

        keps[XY][ndx][Re] = -0.5*(kxy[Y][ndx]*ku[X][ndx][Im] + kxy[X][ndx]*ku[Y][ndx][Im]);
        keps[XY][ndx][Im] =  0.5*(kxy[Y][ndx]*ku[X][ndx][Re] + kxy[X][ndx]*ku[Y][ndx][Re]);
    }

    // keps -> eps
    fftw_execute(plan_strain_xx);
    fftw_execute(plan_strain_yy);
    fftw_execute(plan_strain_xy);

    normalize(eps[0][0], N0, N1, local_n0);
    normalize(eps[1][1], N0, N1, local_n0);
    normalize(eps[0][1], N0, N1, local_n0);
    memcpy(eps[1][0], eps[0][1], sizeof(double)*local_n0*2*(N1/2+1));
}

void calc_lap(double ** lap, fftw_complex ** klap, fftw_complex ** keta, double ** kxy, ptrdiff_t N0, ptrdiff_t N1, ptrdiff_t local_n0)
{
    const int X = 0;
    const int Y = 1;

    for (int p=0; p<3; p++)
    {
        // eta -> keta
        fftw_execute(planF_eta[p]);

        for (int i=0; i<local_n0; i++)
        for (int j=0; j<(N1/2+1); j++)
        {
            int ndx = i*(N1/2+1) + j;
            double k2 = kxy[X][ndx]*kxy[X][ndx] + kxy[Y][ndx]*kxy[Y][ndx];
            //klap[p][ndx][Re] = -k2 * keta[p][ndx][Re];
            //klap[p][ndx][Im] = -k2 * keta[p][ndx][Im];

            double rk = (k2 >= 0.0) ? sqrt(k2) : 0.0;
            double kmod = 2.0*(1.0-cos(rk));
            klap[p][ndx][Re] = -kmod * keta[p][ndx][Re];
            klap[p][ndx][Im] = -kmod * keta[p][ndx][Im];
        }

        // klap -> lap
        fftw_execute(planB_lap[p]);
        normalize(lap[p], N0, N1, local_n0);
    }
}


void output(std::string path, double * data, ptrdiff_t N0, ptrdiff_t N1, ptrdiff_t local_n0)
{
    int np, rank;
    double * buffer;
    int alloc_local = local_n0 * (N1/2+1);
    int tag = 0;
    MPI_Status status;
    int dims[2] = {N0, 2*(N1/2+1)};

    MPI_Comm_size(MPI_COMM_WORLD, &np);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if ( rank == 0 ) {
        buffer = new double [N0*2*(N1/2+1)];
        memcpy(buffer, data, 2*alloc_local*sizeof(double));

        for (int i=1; i<np; i++)
            MPI_Recv(buffer + i*2*alloc_local, 2*alloc_local, MPI_DOUBLE, i, tag, MPI_COMM_WORLD, &status);

        H5File h5;
        h5.open("out.h5", "a");
        h5.write_dataset(path, buffer, dims, 2);
        h5.close();

        delete [] buffer;
    } else {
        MPI_Send(data, 2*alloc_local, MPI_DOUBLE, 0, tag, MPI_COMM_WORLD);
    }


}

std::string zeroFill(int x)
{
    std::stringstream ss;
    ss << std::setw(6) << std::setfill('0') << x;
    return ss.str();
}



void interpolate(double * data, double m0, double m1, double * phi, ptrdiff_t local_n0, ptrdiff_t N1)
{
    const int N1r = 2*(N1/2 + 1);

    for (int i=0; i<local_n0; i++)
    for (int j=0; j<N1; j++)
    {
        int ndx = i*N1r + j;
        data[ndx] = (1-phi[ndx])*m0 + phi[ndx]*m1;
    }
}

double calc_area(double ** eta, ptrdiff_t local_n0, ptrdiff_t N0, ptrdiff_t N1, double norm)
{
    const int N1r = 2*(N1/2+1);
    double sum = 0;
    double threshold = 0.5*norm;
    for (int i=0; i<local_n0; i++)
    for (int j=0; j<N1; j++)
    {
        int ndx = i*N1r + j;
        sum += std::abs(eta[0][ndx]) > threshold ? 1 : 0;
        sum += std::abs(eta[1][ndx]) > threshold ? 1 : 0;
        sum += std::abs(eta[2][ndx]) > threshold ? 1 : 0;
    }

    MPI_Allreduce(MPI_IN_PLACE, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
    return sum/(N0*N1);
}

int main(int argc, char ** argv)
{

    MPI_Init(&argc, &argv);
    fftw_mpi_init();

    srand(time(NULL));

    struct input_parameters ip;

    ParameterFile pf;
    pf.readParameters("input.txt");
    pf.unpack("Nx", ip.Nx);
    pf.unpack("Ny", ip.Ny);
    pf.unpack("dx", ip.dx);
    pf.unpack("dt", ip.dt);

    pf.unpack("mu_el", ip.mu_el);
    pf.unpack("nu_el", ip.nu_el);
    pf.unpack("nsteps", ip.nsteps);
    pf.unpack("out_freq", ip.out_freq);

    pf.unpack("epsx", ip.epsx);
    pf.unpack("epsy", ip.epsy);

    pf.unpack("gamma", ip.gamma);
    pf.unpack("kappa", ip.kappa);
    pf.unpack("alpha", ip.alpha);
    pf.unpack("change_etap_thresh", ip.change_etap_thresh);
    pf.unpack("beta", ip.beta);

    pf.unpack("M0_chem_a", ip.M0_chem_a);
    pf.unpack("M0_chem_b", ip.M0_chem_b);
    pf.unpack("M0_chem_c", ip.M0_chem_c);

    pf.unpack("M1_chem_a", ip.M1_chem_a);
    pf.unpack("M1_chem_b", ip.M1_chem_b);
    pf.unpack("M1_chem_c", ip.M1_chem_c);

    pf.unpack("M0_2H_a", ip.M0_2H_a);
    pf.unpack("M0_2H_b", ip.M0_2H_b);
    pf.unpack("M0_Tp_a", ip.M0_Tp_a);
    pf.unpack("M0_Tp_b", ip.M0_Tp_b);

    pf.unpack("M1_2H_a", ip.M1_2H_a);
    pf.unpack("M1_2H_b", ip.M1_2H_b);
    pf.unpack("M1_Tp_a", ip.M1_Tp_a);
    pf.unpack("M1_Tp_b", ip.M1_Tp_b);

    pf.unpack("M0_norm", ip.M0_norm);
    pf.unpack("M1_norm", ip.M1_norm);

    ptrdiff_t local_n0;
    ptrdiff_t local_0_start;
    ptrdiff_t N0 = (ptrdiff_t) ip.Nx;
    ptrdiff_t N1 = (ptrdiff_t) ip.Ny;

    ptrdiff_t alloc_local = fftw_mpi_local_size_2d(N0, N1/2+1, MPI_COMM_WORLD, &local_n0, &local_0_start);

    double *** G    = (double ***) kd_alloc2(sizeof(double), 3, 2, 2, alloc_local);
    double ** kxy   = (double **) kd_alloc2(sizeof(double), 2, 2, alloc_local);

    double **** epsT = (double ****) kd_alloc2(sizeof(double), 4, 2, 3, 2, 2);

    double **** eps0 = (double ****) kd_alloc2(sizeof(double), 4, 3, 2, 2, 2*alloc_local);
    double **** sig0 = (double ****) kd_alloc2(sizeof(double), 4, 3, 2, 2, 2*alloc_local);
    double *** sigeps = (double ***) kd_alloc2(sizeof(double), 3, 3, 3, 2*alloc_local);

    double *** eps  = (double ***) kd_alloc2(sizeof(double), 3, 2, 2, 2*alloc_local);
    double **** lam = (double ****) kd_alloc2(sizeof(double), 4, 2, 2, 2, 2);
    double ** epsbar = (double **) kd_alloc2(sizeof(double), 2, 2, 2);
    double *** s0n2 = (double ***) kd_alloc2(sizeof(double), 3, 3, 3, 2*alloc_local);
    fftw_complex *** ks0n2 = (fftw_complex ***) kd_alloc2(sizeof(fftw_complex), 3, 3, 3, alloc_local);

/*
    for (int p=0; p<3; p++)
    for (int i=0; i<3; i++)
    {
        s0n2[p][i] = fftw_alloc_real(2*alloc_local);
        ks0n2[p][i] = fftw_alloc_complex(alloc_local);
    }
    */

    double ** chem = new double * [3];
    chem[0] = fftw_alloc_real(2*alloc_local);
    chem[1] = fftw_alloc_real(2*alloc_local);
    chem[2] = fftw_alloc_real(2*alloc_local);

    double ** eta = new double * [3];
    eta[0] = fftw_alloc_real(2*alloc_local);
    eta[1] = fftw_alloc_real(2*alloc_local);
    eta[2] = fftw_alloc_real(2*alloc_local);

    double ** eta_old = new double * [3];
    eta_old[0] = fftw_alloc_real(2*alloc_local);
    eta_old[1] = fftw_alloc_real(2*alloc_local);
    eta_old[2] = fftw_alloc_real(2*alloc_local);

    double ** eta_new = new double * [3];
    eta_new[0] = fftw_alloc_real(2*alloc_local);
    eta_new[1] = fftw_alloc_real(2*alloc_local);
    eta_new[2] = fftw_alloc_real(2*alloc_local);

    double ** lap = new double * [3];
    lap[0] = fftw_alloc_real(2*alloc_local);
    lap[1] = fftw_alloc_real(2*alloc_local);
    lap[2] = fftw_alloc_real(2*alloc_local);

    fftw_complex ** ketapsq = new fftw_complex * [3];
    ketapsq[0] = fftw_alloc_complex(alloc_local);
    ketapsq[1] = fftw_alloc_complex(alloc_local);
    ketapsq[2] = fftw_alloc_complex(alloc_local);

    fftw_complex ** keta = new fftw_complex * [3];
    keta[0] = fftw_alloc_complex(alloc_local);
    keta[1] = fftw_alloc_complex(alloc_local);
    keta[2] = fftw_alloc_complex(alloc_local);

    fftw_complex ** klap = new fftw_complex * [3];
    klap[0] = fftw_alloc_complex(alloc_local);
    klap[1] = fftw_alloc_complex(alloc_local);
    klap[2] = fftw_alloc_complex(alloc_local);

    fftw_complex ** keps = new fftw_complex * [3];
    keps[0] = fftw_alloc_complex(alloc_local);
    keps[1] = fftw_alloc_complex(alloc_local);
    keps[2] = fftw_alloc_complex(alloc_local);

    double * ux = fftw_alloc_real(2*alloc_local);
    double * uy = fftw_alloc_real(2*alloc_local);
    double * phi = fftw_alloc_real(2*alloc_local);
    double * lsf = fftw_alloc_real(local_n0*N1);

    fftw_complex ** ku = new fftw_complex * [2];
    ku[0] = fftw_alloc_complex(alloc_local);
    ku[1] = fftw_alloc_complex(alloc_local);

    planF_eta[0] = fftw_mpi_plan_dft_r2c_2d(N0, N1, eta[0], keta[0], MPI_COMM_WORLD, FFTW_MEASURE);
    planF_eta[1] = fftw_mpi_plan_dft_r2c_2d(N0, N1, eta[1], keta[1], MPI_COMM_WORLD, FFTW_MEASURE);
    planF_eta[2] = fftw_mpi_plan_dft_r2c_2d(N0, N1, eta[2], keta[2], MPI_COMM_WORLD, FFTW_MEASURE);

    planB_lap[0] = fftw_mpi_plan_dft_c2r_2d(N0, N1, klap[0], lap[0], MPI_COMM_WORLD, FFTW_MEASURE);
    planB_lap[1] = fftw_mpi_plan_dft_c2r_2d(N0, N1, klap[1], lap[1], MPI_COMM_WORLD, FFTW_MEASURE);
    planB_lap[2] = fftw_mpi_plan_dft_c2r_2d(N0, N1, klap[2], lap[2], MPI_COMM_WORLD, FFTW_MEASURE);

    for (int p=0; p<3; p++)
    for (int i=0; i<3; i++)
        planF_s0n2[p][i] = fftw_mpi_plan_dft_r2c_2d(N0, N1, s0n2[p][i], ks0n2[p][i], MPI_COMM_WORLD, FFTW_MEASURE);

    planB_ux = fftw_mpi_plan_dft_c2r_2d(N0, N1, ku[0], ux, MPI_COMM_WORLD, FFTW_MEASURE);
    planB_uy = fftw_mpi_plan_dft_c2r_2d(N0, N1, ku[1], uy, MPI_COMM_WORLD, FFTW_MEASURE);

    plan_strain_xx = fftw_mpi_plan_dft_c2r_2d(N0, N1, keps[0], eps[0][0], MPI_COMM_WORLD, FFTW_MEASURE);
    plan_strain_yy = fftw_mpi_plan_dft_c2r_2d(N0, N1, keps[1], eps[1][1], MPI_COMM_WORLD, FFTW_MEASURE);
    plan_strain_xy = fftw_mpi_plan_dft_c2r_2d(N0, N1, keps[2], eps[0][1], MPI_COMM_WORLD, FFTW_MEASURE);

    calc_greens_function(G, kxy, local_n0, local_0_start, N1, ip);
    calc_transformation_strains(epsT, ip);

    initialize_lsf_circle(lsf, local_n0, local_0_start, N1);
    //initialize_lsf_stripe(lsf, local_n0, local_0_start, N1);
    //initialize_lsf_zigzag(lsf, local_n0, local_0_start, N1);
    diffuse_lsf(lsf, local_n0, N1);
    copy_lsf(lsf, phi, local_n0, N1);

    for (int p=0; p<3; p++)
    for (int i=0; i<2; i++)
    for (int j=0; j<2; j++)
        interpolate(eps0[p][i][j], epsT[0][p][i][j], epsT[1][p][i][j], phi, local_n0, N1);

    calc_elastic_tensors(lam, eps0, sig0, sigeps, ip.mu_el, ip.nu_el, local_n0, N1);

    log_greens_function(G, kxy, local_n0, N1);
    log_elastic_tensors(lam, epsT);

    initialize(eta, eta_old, local_n0, N1);

    for (int i=0; i<2*alloc_local; i++) { ux[i] = 0; uy[i] = 0; }

    H5File h5;
    h5.open("out.h5", "w");
    output("phi", phi, N0, N1, local_n0);
    h5.close();

    FILE * fp = fopen("area_fraction.dat", "w");
    fclose(fp);

    int frame = 0;
    for (int step=1; step<=ip.nsteps; step++)
    {
        epsbar[0][0] = ip.epsx * (step/(double)ip.nsteps);
        epsbar[1][1] = ip.epsy * (step/(double)ip.nsteps);
        epsbar[0][1] = 0;
        epsbar[1][0] = 0;

        double change_etap_max = 1;
        double area_fraction;
        while (change_etap_max > ip.change_etap_thresh)
        {
            change_etap_max = 0;

            calc_ks0n2(s0n2, sig0, eta, local_n0, N1);
            calc_uxy(ux, uy, ku, G, kxy, ks0n2, N0, N1, local_n0);

            calc_eps(eps, keps, kxy, ku, N0, N1, local_n0);

            introduce_noise(eta, local_n0, N1);
            calc_lap(lap, klap, keta, kxy, N0, N1, local_n0);

            calc_chemical_potential(chem, eta, sigeps, epsbar, sig0, eps, lap, phi, local_n0, N1, ip);

            change_etap_max = update_eta(eta, eta_old, eta_new, chem, local_n0, N1, ip);

            MPI_Allreduce(MPI_IN_PLACE, &change_etap_max, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

            area_fraction = calc_area(eta, local_n0, N0, N1, ip.M1_norm);
            printf("%8d cepmax=%12.10f, Af=%12.10f\n",step,change_etap_max,area_fraction);
        }

        fp = fopen("area_fraction.dat", "a");
        fprintf(fp, "%10d %12.10f\n", step, area_fraction);
        fclose(fp);

        if (step % ip.out_freq == 0) {
            frame++;
            output("eta0/"+zeroFill(frame), eta[0], N0, N1, local_n0);
            output("eta1/"+zeroFill(frame), eta[1], N0, N1, local_n0);
            output("eta2/"+zeroFill(frame), eta[2], N0, N1, local_n0);
            output("eps_xx/"+zeroFill(frame), eps[0][0], N0, N1, local_n0);
            output("eps_yy/"+zeroFill(frame), eps[1][1], N0, N1, local_n0);
            output("eps_xy/"+zeroFill(frame), eps[0][1], N0, N1, local_n0);
            output("ux/"+zeroFill(frame), ux, N0, N1, local_n0);
            output("uy/"+zeroFill(frame), uy, N0, N1, local_n0);
        }
    }

    fftw_mpi_cleanup();
    MPI_Finalize();

    return 0;
}