/*
 * SUMMARY:      CalcBlowingSnow.c - Calculate energy of sublimation from blowing snow
 * USAGE:        Part of VIC
 *
 * AUTHOR:       Laura Bowling
 * ORG:          University of Washington, Department of Civil Engineering
 * E-MAIL:              lbowling@u.washington.edu
 * ORIG-DATE:     3-Feb-2002 
 * LAST-MOD: 
 * DESCRIPTION:  Calculate blowing snow
 * DESCRIP-END.
 * FUNCTIONS:    CalcBlowingSnow()
 * COMMENTS:     
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <vicNl.h>
#include <mtclim42_vic.h>

#define GRAMSPKG 1000.
#define CH_WATER 4186.8e3
#define JOULESPCAL     4.1868   /* Joules per calorie */
#define Ka  .0245187            /* thermal conductivity of air (W/mK) */
#define CSALT 0.68              /* saltation constant m/s */
#define UTHRESH 0.25            /* threshold shear velocity m/s */
#define KIN_VIS 1.3e-5          /* Kinemativ viscosity of air (m2/s) */
#define MAX_ITER 100             /* Max. iterations for numerical integration */
#define K 5
#define MACHEPS 1.0e-6          /* Accuracy tolerance for numerical integration */
#define SETTLING 0.3            /* Particle settling velocity m/s */
#define UPARTICLE 2.8*UTHRESH   /* Horizontal particle velocity m/s */
                                /* After Pomeroy and Gray (1990) */
#define NUMINCS 10              /* Number of prob intervals to solve for wind. */
#define LAPLACEK 1.                      /* Fit parameter of the laplace distribution. */ 
#define SIMPLE 0               /* SBSM (1) or Liston & Sturm (0) mass flux */
#define SPATIAL_WIND 1         /* Variable (1) or constant (0) wind distribution. */
#define VAR_THRESHOLD 1         /* Variable (1) or constant (0) threshold shear stress. */
#define FETCH 1               /* Include fetch dependence (1). */
#define CALC_PROB 1             /* Variable (1) or constant (0) probability of occurence. */

void polint(double xa[], double ya[], int n, double x, double *y, double *dy);
double trapzd(double a, double b, int n, double es, double Wind, double AirDens, double ZO, double EactAir, 
	      double F, double hsalt, double phi_r, double ushear, double Zrh);
double qromb(double a, double b, double es, double Wind, double AirDens, double ZO, double EactAir, 
	     double F, double hsalt, double phi_r, double ushear, double Zrh);
double sub_with_height(double z,double es,  double Wind, double AirDens, double ZO,          
		       double EactAir,double F, double hsalt, double phi_r,         
		       double ushear, double Zrh, int flag);
double rtnewt(double x1, double x2, double xacc, double Ur, double Zr);
void get_shear(double x, double *f, double *df, double Ur, double Zr);
double get_prob(double Tair, double Age, double SurfaceLiquidWater, double U10);
double get_thresh(double Tair, double SurfaceLiquidWater, double U10, double Zo_salt, double prob_occurence, int flag, double ushear);
void shear_stress(double U10, double ZO,double *ushear, double *Zo_salt);
double CalcSubFlux(double EactAir, double es, double Zrh, double AirDens, double utshear, 
		   double ushear, double fe, double Tsnow, double Tair, double U10, double Zo_salt, double F);

/*****************************************************************************
  Function name: CalcBlowingSnow()

  Purpose      : Calculate sublimation from blowing snow

  Required     :  double Dt;                     Model time step (hours) 
  double Tair;                    Air temperature (C) 
  int LastSnow;                   Time steps since last snowfall. 
  double SurfaceLiquidWater;      Liquid water in the surface layer (m) 
  double Wind;                    Wind speed (m/s), 2 m above snow 
  double Ls;                      Latent heat of sublimation (J/kg) 
  double AirDens;                 Density of air (kg/m3) 
  double Lv;                      Latent heat of vaporization (J/kg3) 
  double Press;                   Air pressure (Pa) 
  double EactAir;                 Actual vapor pressure of air (Pa) 
  double ZO;                      Snow roughness height (m)
  Returns      : BlowingMassFlux

  Modifies     : 

  Comments     : Called from SnowPackEnergyBalance
    Reference:  

*****************************************************************************/
double CalcBlowingSnow( double Dt, 
			double Tair,
			int LastSnow,
			double SurfaceLiquidWater,
			double Wind,
			double Ls,
			double AirDens,
			double Press,
			double EactAir,
			double *ZO,
			double Zrh,
			double snowdepth,
			float lag_one,
			float sigma_slope,
			double Tsnow, 
			int iveg, 
			int Nveg, 
			float fe,
			double displacement,
			double roughness)

{
  /* Local variables: */

  double Age;
  double U10, Uo, prob_occurence;
  double es, Ros, F;
  double SubFlux;
  double Diffusivity;
  double ushear, Qsalt, hsalt, phi_s, psi_s;
  double Tk;
  double Lv;
  double T, ztop;
  double ut10, utshear;
  int p;
  double upper, lower, Total;
  double area;
  double sigma_w;
  double undersat_2;
  double b, temp2; /* SBSM scaling parameter. */
  double temp, temp3;
  double Zo_salt;
  double ratio, wind10;
  double Uveg, hv, Nd;

  Lv = (2.501e6 - 0.002361e6 * Tsnow);
  /*******************************************************************/
  /* Calculate some general variables, that don't depend on wind speed. */

  Age = LastSnow*(Dt);
      
  /* Saturation density of water vapor, Liston A-8 */
  es = svp(Tair);
  Tk = Tair  + KELVIN;
    
  Ros = 0.622*es/(287*Tk);
  
  /* Diffusivity in m2/s, Liston eq. A-7 */
  Diffusivity = (2.06e-5) * pow(Tk/273.,1.75);

  // Essery et al. 1999, eq. 6 (m*s/kg)
  F = (Ls/(Ka*Tk))*(Ls*MW/(R*Tk) - 1.);
  F += 1./(Diffusivity*Ros);

  /* grid cell 10 m wind speed = 50th percentile wind */
  /* Wind speed at 2 m above snow was passed to this function. */

  wind10 = Wind*log(10./ZO[2])/log((2+ZO[2])/ZO[2]);
  //  fprintf(stderr,"wind=%f, Uo=%f\n",Wind, Uo);

  /* Check for bare soil case. */
  if(iveg == Nveg) {
    fe = 1500;
    sigma_slope = .0002;
  }
  // sigma_w/uo:
  ratio = (2.4 - (0.4/0.9)*lag_one)*sigma_slope;
  //  sigma_w = wind10/(.69+(1/ratio));
  //  Uo = sigma_w/ratio;
  
  sigma_w = wind10*ratio;

  /* Hack to keep model running until I find bug.*/
  if(sigma_w > 10. || sigma_w < -10.) {
    sigma_w = 0.22;
    fprintf(stderr, "sigma problem:\n");
    fprintf(stderr, "sigma_W =%f, wind10 = %f, Wind=%f\n",sigma_w, wind10, Wind);
    fprintf(stderr, "lagone=%f, sigma_slope=%f\n",lag_one, sigma_slope);
  }
	
  Uo = wind10;
  
  /*************/
  /* Parameters for roughness above snow. */
  hv = (3./2.)*displacement;
  Nd = (4./3.)*(roughness/displacement);

  /*******************************************************************/
  /** Begin loop through wind probability function.                  */

  Total = 0.0;
  area = 1./NUMINCS;
  
  /* Assume snow holding capacity is twice the roughness length. */
  /* No snow is transported until holding capacity is satisfied. */

  if(snowdepth > 0.0) {
    if(SPATIAL_WIND && sigma_w != 0.) {
      for(p= 0; p< NUMINCS; p++) {
      
      SubFlux = lower = upper = 0.0;
      /* Find the limits of integration. */
      if(p==0) {
	lower = 0;
	upper = Uo + sigma_w*log(2.*(p+1)*area);
      }
      else if(p > 0 && p < NUMINCS/2) {
	lower = Uo + sigma_w*log(2.*(p)*area);
	upper = Uo + sigma_w*log(2.*(p+1)*area);
      }
      else if(p < (NUMINCS-1) && p >= NUMINCS/2) {
	lower = Uo - sigma_w*log(2.-2.*(p*area));
	upper = Uo - sigma_w*log(2.-2.*((p+1.)*area));
      }
      else if(p == NUMINCS-1) {
	lower =  Uo - sigma_w*log(2.-2.*(p*area));
	upper = Uo*2.;
      }

      if(lower < 0.)
	lower = 0.;
      if(upper < 0)
	upper = 0.;
      if(lower > upper)  /* Could happen if lower > Uo*2 */
	lower = upper;

      /* Find expected value of wind speed for the interval. */
      U10 = Uo;
      if(lower >= Uo ) 
	U10 = -0.5*((upper+sigma_w)*exp((-1./sigma_w)*(upper - Uo))
		    - (lower+sigma_w)*exp((-1./sigma_w)*(lower - Uo)))/area;
      else if(upper <= Uo )
	U10 = 0.5*((upper-sigma_w)*exp((1./sigma_w)*(upper - Uo))
		   - (lower-sigma_w)*exp((1./sigma_w)*(lower - Uo)))/area;
      else {
	fprintf(stderr,"Problem with probability ranges in CalcBlowingSnow.c\n");
	fprintf(stderr,"Increment = %d, integration limits = %f - %f\n",p,upper, lower);
	fprintf(stderr, "sigma_W =%f, Uo = %f, area = %f\n",sigma_w, Uo, area);
	fprintf(stderr, "lagone=%f, sigma_slope=%f\n",lag_one, sigma_slope);
	U10 = 0.4;
      }
   
      if(U10 < 0.4)
	U10 = .4;

      if(U10 > 25.) U10 = 25.;
      /*******************************************************************/
      /* Calculate parameters for probability of blowing snow occurence. */
      /* ( Li and Pomeroy 1997) */

      if(snowdepth < hv)
	Uveg = U10/sqrt(1.+ 680*Nd*(hv - snowdepth));
      else
	Uveg = U10;
  
      //   printf("Uveg = %f, U10 = %f\n",Uveg, U10);

      prob_occurence = get_prob(Tair, Age, SurfaceLiquidWater, Uveg);

      /* Iterate to find actual shear stress during saltation. */

      shear_stress(U10, ZO[2], &ushear, &Zo_salt);
     

      /*******************************************************************/
      /* Calculate threshold shear stress. Send 0 for constant or  */
      /* 1 for variable threshold after Li and Pomeroy (1997)      */

      utshear = get_thresh(Tair, SurfaceLiquidWater, U10, ZO[2], prob_occurence, VAR_THRESHOLD, ushear);
      
      
      if(ushear > utshear && EactAir < es) {
	
	SubFlux = CalcSubFlux(EactAir, es, Zrh, AirDens, utshear,ushear, fe, Tsnow, Tair, U10, Zo_salt, F);}
      else
	SubFlux=0.0;
  
      Total += (1./NUMINCS)*SubFlux*prob_occurence;
    
      //   fprintf(stderr, "iveg=%d,U10=%f, prob_occurence=%f, Total=%f\n",iveg,U10, prob_occurence,Total);
      }
   
    }
    else {
      Uo=wind10;
       /*******************************************************************/
      /* Calculate parameters for probability of blowing snow occurence. */
      /* ( Li and Pomeroy 1997) */
  
      if(snowdepth < hv)
	Uveg = U10/sqrt(1.+ 680*Nd*(hv - snowdepth));
      else
	Uveg = U10;

      prob_occurence = get_prob(Tair, Age, SurfaceLiquidWater, Uveg);
    
      /* Iterate to find actual shear stress during saltation. */

      shear_stress(Uo, ZO[2], &ushear, &Zo_salt);
      

      /*******************************************************************/
      /* Calculate threshold shear stress. Send 0 for constant or  */
      /* 1 for variable threshold after Li and Pomeroy (1997)      */

      utshear = get_thresh(Tair, SurfaceLiquidWater, Uo, ZO[2], prob_occurence, VAR_THRESHOLD, ushear);

      if(ushear > utshear && EactAir < es) {
	SubFlux = CalcSubFlux(EactAir, es, Zrh, AirDens, utshear,ushear, fe, Tsnow, Tair, Uo, Zo_salt, F);
      }
      else
	SubFlux=0.0;

      Total = SubFlux*prob_occurence;
    }
  }

  if(Total < -.00005)
    Total = -.00005;
 
  return Total;
  
}

double qromb(double a, double b, double es, double Wind, double AirDens, double ZO, double EactAir, 
	     double F, double hsalt, double phi_r, double ushear, double Zrh)
     // Returns the integral of the function func from a to b.  Integration is performed by Romberg's method:
     // Numerical Recipes in C Section 4.3
{
  double ss, dss;
  double s[MAX_ITER+1], h[MAX_ITER+2];
  int j;

  h[1] = 1.0;
  for(j=1; j<=MAX_ITER; j++) {
    s[j]=trapzd(a,b,j,es, Wind, AirDens, ZO, EactAir, F, hsalt, phi_r, ushear, Zrh);
    if(j >= K) {
      polint(&h[j-K],&s[j-K],K,0.0,&ss,&dss);
      if (fabs(dss) <= MACHEPS*fabs(ss)) return ss;
    }
    h[j+1]=0.25*h[j];
  }
  nrerror("Too many steps in routine qromb");
  return 0.0;
}

void polint(double xa[], double ya[], int n, double x, double *y, double *dy)
{
  int i, m, ns;
  double den, dif, dift, ho, hp, w;
  double *c,*d;

  ns=1;
  dif=fabs(x-xa[1]);
  c=(double *)malloc((size_t) (n*sizeof(double)));
  if(!c) nrerror("allocation failure in vector()");
  d=(double *)malloc((size_t) (n*sizeof(double)));
  if(!d) nrerror("allocation failure in vector()");

  for (i=1; i<=n; i++) {
    if ( (dift=fabs(x-xa[i])) < dif) {
      ns=i;
      dif=dift;
    }
    c[i]=ya[i];
    d[i]=ya[i];
  }
  *y=ya[ns--];
  for(m=1;m<n;m++) {
    for(i=1; i<=n-m; i++) {
      ho=xa[i]-x;
      hp=xa[i+m]-x;
      w=c[i+1]-d[i];
      if ( (den=ho-hp) == 0.0) nrerror("Error in routine polint");
      den = w/den;
      d[i]=hp*den;
      c[i]=ho*den;
    }
    *y += (*dy=(2*ns < (n-m) ? c[ns+1] : d[ns--]));
  }
  free(d);
  free(c);
}

double trapzd(double a, double b, int n, double es, double Wind, double AirDens, double ZO, double EactAir, 
	     double F, double hsalt, double phi_r, double ushear, double Zrh)
{
  va_list ap;
  double x, tnm, sum, del;
  static double s;
  int it, j;

  if (n==1) {
    return (s=0.5*(b-a)*(sub_with_height(a, es, Wind, AirDens, ZO, EactAir, F, hsalt, 
					 phi_r, ushear, Zrh, 0) + 
			 sub_with_height(b,es, Wind, AirDens, ZO, EactAir, F, hsalt, 
					 phi_r, ushear, Zrh, 0)));
  }
  else {
    for (it=1, j=1; j<n-1; j++) it <<= 1;
    tnm=it;
    del=(b-a)/tnm;
    x=a+0.5*del;
    for(sum=0.0, j=1; j<=it; j++, x+=del ) sum += sub_with_height(x,es, Wind, AirDens, ZO, EactAir, F, hsalt, 
					 phi_r, ushear, Zrh, 0);
    s=0.5*(s+(b-a)*sum/tnm);
    return s;
  }
}

double rtnewt(double x1, double x2, double acc, double Ur, double Zr)
{
  int j;
  double df, dx, dxold, f, fh, fl;
  double temp, xh, xl, rts;

    get_shear(x1,&fl,&df, Ur, Zr);
    get_shear(x2,&fh,&df, Ur, Zr);

    if ((fl > 0.0 && fh > 0.0) || (fl < 0.0 && fh < 0.0)) {
      fprintf(stderr, "Root must be bracketed in rtnewt.\n");
      exit(0);
    }

    if (fl == 0.0) return x1;
    if (fh == 0.0) return x2;
    if (fl < 0.0) {
      xl=x1;
      xh=x2; }
    else {
      xh=x1;
      xl=x2;
    }
    rts=0.5*(x1+x2);
    dxold=fabs(x2-x1);
    dx=dxold;
    get_shear(rts,&f,&df, Ur, Zr);
    for(j=1; j<=MAX_ITER; j++) {
      if((((rts-xh)*df-f)*((rts-x1)*df-f) > 0.0)
	 || (fabs(2.0*f) > fabs(dxold*df))) {
	dxold=dx;
	dx=0.5*(xh-xl);
	rts=xl+dx;
	if (xl == rts) return rts; }
      else {
	dxold=dx;
	dx=f/df;
	temp=rts;
	rts -= dx;
	if (temp == rts) return rts;
      }
      if(fabs(dx) < acc) return rts;
      // if(rts < .025) rts=.025;
  get_shear(rts,&f,&df, Ur, Zr);
       if(f<0.0)
	 xl=rts;
       else 
	 xh = rts;
    }
    fprintf(stderr, "Maximum number of iterations exceeded in rtnewt.\n");
    rts = .025;
      return rts;
}

void get_shear(double x, double *f, double *df, double Ur, double Zr) {

  *f = exp(von_K*Ur/x) - (2.*G_STD*Zr)/(.12*x*x);
  *df = -1.*exp(von_K*Ur/x)/(x*x) + 2.*(2.*G_STD*Zr)/(.12*x*x*x);
}

/*****************************************************************************
  Function name: sub_with_height()

  Purpose      : Calculate the sublimation rate for a given height above the boundary layer.

  Required     :
    double z               - Height of solution (m) 
    double Tair;           - Air temperature (C) 
    double Wind;           - Wind speed (m/s), 2 m above snow 
    double AirDens;        - Density of air (kg/m3) 
    double ZO;             - Snow roughness height (m)
    double EactAir;        - Actual vapor pressure of air (Pa) 
    double F;              - Denominator of dm/dt          
    double hsalt;          - Height of the saltation layer (m)
    double phi_r;          - Saltation layer mass concentration (kg/m3)
    double ushear;         - shear velocity (m/s) 
    double Zrh;            - Reference height of humidity measurements

  Returns      :
   double f(z)             - Sublimation rate in kg/m^3*s

  Modifies     : none

  Comments     :  Currently neglects radiation absorption of snow particles.
*****************************************************************************/
double sub_with_height(double z,
		       double es,          
		       double Wind,          
		       double AirDens,     
		       double ZO,          
		       double EactAir,        
		       double F,          
		       double hsalt,         
		       double phi_r,         
		       double ushear,        
		       double Zrh,
		       int flag)
{

  /* Local variables */
  double Rrz, ALPHAz, Mz;
  double Rmean, terminal_v, fluctuat_v;
  double Vtz, Re, Nu;
  double sigz, dMdt;
  double temp;
  double psi_t, phi_t;

 
  //  Calculate sublimation loss rate (1/s)
  Rrz = 4.6e-5* pow (z, -.258);
  ALPHAz = 4.08 + 12.6 * z;
  Mz = (4./3.) * PI * ice_density * Rrz * Rrz * Rrz *(1. +(3./ALPHAz) + (2./(ALPHAz*ALPHAz)));

  Rmean = pow((3.*Mz)/(4.*PI*ice_density),1./3.);

  // Pomeroy and Male 1986
  terminal_v = 1.1e7 * pow(Rmean,1.8);

  // Pomeroy (1988)
  fluctuat_v = 0.005 * pow(Wind, 1.36);

  // Ventilation velocity for turbulent suspension Lee (1975)
  Vtz = terminal_v + 3.*fluctuat_v*cos(PI/4.);

  Re = 2. * Rmean * Vtz / KIN_VIS;
  Nu = 1.79 + 0.606 * pow(Re, 0.5);

  sigz = ((EactAir/es) - 1.) * (1 - .027*log( z / Zrh));
  dMdt = 2 * PI * Rmean * sigz * Nu / F;
  // sublimation loss rate coefficient (1/s)

  psi_t = dMdt/Mz;

  // Concentration of turbulent suspended snow Kind (1992)
  
  temp = (0.5*ushear*ushear)/(Wind*SETTLING);
  phi_t = phi_r* ( (temp + 1.) * pow((z/hsalt),(-1.*SETTLING)/(von_K*ushear)) - temp );

  
  if(flag==1) 
    return psi_t;
  else
    return psi_t * phi_t;
}

/*******************************************************************/
/* Calculate parameters for probability of blowing snow occurence. */
/* ( Li and Pomeroy 1997) */
/*******************************************************************/

double get_prob(double Tair, double Age, double SurfaceLiquidWater, double U10) 
{
  double mean_u_occurence;
  double sigma_occurence;
  double prob_occurence;

  if(CALC_PROB) {
    if(SurfaceLiquidWater < 0.001) {
      mean_u_occurence = 11.2 + 0.365*Tair + 0.00706*Tair*Tair+0.9*log(Age);
      sigma_occurence = 4.3 + 0.145*Tair + 0.00196*Tair*Tair;
      if(U10 > 3.)
	prob_occurence = 1./(1.+exp(sqrt(PI)*(mean_u_occurence-U10)/sigma_occurence));
      else
	prob_occurence = 0.0;
      
    }
    else {
      mean_u_occurence = 21.;
      sigma_occurence = 7.;
	
      if(U10 > 7.)
	prob_occurence = 1./(1.+exp(sqrt(PI)*(mean_u_occurence-U10)/sigma_occurence));
      else
	prob_occurence = 0.0;
    }
  }
  else
    prob_occurence = 1.;

  return prob_occurence;
}

double get_thresh(double Tair, double SurfaceLiquidWater, double U10, double Zo_salt, 
		  double prob_occurence, int flag, double ushear) 
{
  double ut10;
  double utshear;

  if(SurfaceLiquidWater < 0.001) {
    // Threshold wind speed after Li and Pomeroy (1997)
    ut10 = 9.43 + .18 * Tair + .0033 * Tair*Tair;
  }
  else {
    
    // Threshold wind speed after Li and Pomeroy (1997)
    ut10 = 9.9;
  }

  if(flag) {
    // Variable threshold, Li and Pomeroy 1997
    utshear = von_K * ut10 / log(10./Zo_salt);
    if(ushear < utshear && prob_occurence > 0.001)
      utshear = von_K * (U10 - .5) / log(10./Zo_salt);
  }
      // Constant threshold, i.e. Liston and Sturm
  else
    utshear = UTHRESH;
  
  return utshear;
}


void shear_stress(double U10, double ZO,double *ushear, double *Zo_salt)
{
  double temp;
  
  /* Iterate to find actual shear stress. */
  temp = von_K * U10 / log(10./ZO);
  // fprintf(stderr,"x1=.0000001, x2=%f, U10=%f\n",temp+5, U10);
  *ushear = rtnewt (.0000001, temp+5, .000001, U10, 10.);
  *Zo_salt = 0.12 *(*ushear) * (*ushear) / (2.* G_STD);

  if(*Zo_salt < ZO) {
    *Zo_salt = ZO;
    *ushear = temp;
  }

}

double CalcSubFlux(double EactAir, double es, double Zrh, double AirDens, double utshear, 
		   double ushear, double fe, double Tsnow, double Tair, double U10, double Zo_salt, double F)

{
  float b, undersat_2;
  double SubFlux;
  double Qsalt, hsalt;
  double phi_s, psi_s;
  double T, ztop;
  double particle;

  SubFlux=0.0;
  particle = utshear*2.8;
  // SBSM:
  if(SIMPLE) {   
    b=.25;
    undersat_2 = ((EactAir/es)-1.)*(1.-.027*log(Zrh)+0.027*log(2)); 
    //  fprintf(stderr,"RH=%f\n",EactAir/es);
    SubFlux =  b*undersat_2* pow(U10, 5.) / F;
  }

  else {
    //  Sublimation flux (kg/m2*s) = mass-concentration * sublimation rate * height
    //  for both the saltation layer and the suspension layer
	  
    //  Saltation layer is assumed constant with height
    // Maximum saltation transport rate (kg/m*s)
    // Liston and Sturm 1998, eq. 6
    Qsalt = ( CSALT * AirDens /  G_STD ) * (utshear / ushear) * (ushear*ushear - utshear*utshear); 
    if(FETCH)
      Qsalt *= (1.+(500./(3.*fe))*(exp(-3.*fe/500.)-1.));
    hsalt = 1.6 * ushear * ushear / ( 2. * G_STD );
	  
    // Saltation layer mass concentration (kg/m3)
    phi_s = Qsalt / (hsalt * particle);
	  
    // Sublimation loss-rate for the saltation layer (s-1)
    psi_s =  sub_with_height(hsalt/2.,es, U10, AirDens, Zo_salt, EactAir, F, hsalt, 
				 phi_s, ushear, Zrh, 1);
    
    // Sublimation from the saltation layer in kg/m2*s
    SubFlux = phi_s*psi_s*hsalt;
   
    T = 0.5*(ushear*ushear)/(U10*SETTLING);
    ztop = hsalt*pow(T/(T+1.), (von_K*ushear)/(-1.*SETTLING));
    //  Suspension layer must be integrated
    SubFlux += qromb(hsalt, ztop, es, U10, AirDens, Zo_salt, EactAir, F, hsalt, phi_s, ushear, Zrh);
  }

  return SubFlux;
}