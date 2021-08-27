#ifdef USE_NPROF
#include <nvToolsExt.h> 
#endif
#include <cmath>
#include <limits>
#include <iostream>
#ifdef USE_OMP
#include "omp.h"
#endif

#include "constants.hpp"
#include "parameters.hpp"
#include "matprops.hpp"
#include "utils.hpp"
#include "geometry.hpp"
#include "bc.hpp"


/* Given two points, returns the distance^2 */
double dist2(const double* a, const double* b)
{
    double sum = 0;
    for (int i=0; i<NDIMS; ++i) {
        double d = b[i] - a[i];
        sum += d * d;
    }
    return sum;
}


/* Given four 3D points, returns the (signed) volume of the enclosed
   tetrahedron */
static double tetrahedron_volume(const double *d0,
                                 const double *d1,
                                 const double *d2,
                                 const double *d3)
{
    double x01 = d0[0] - d1[0];
    double x12 = d1[0] - d2[0];
    double x23 = d2[0] - d3[0];

    double y01 = d0[1] - d1[1];
    double y12 = d1[1] - d2[1];
    double y23 = d2[1] - d3[1];

    double z01 = d0[2] - d1[2];
    double z12 = d1[2] - d2[2];
    double z23 = d2[2] - d3[2];

    return (x01*(y23*z12 - y12*z23) +
            x12*(y01*z23 - y23*z01) +
            x23*(y12*z01 - y01*z12)) / 6;
}


/* Given two points, returns the area of the enclosed triangle */
static double triangle_area(const double *a,
                            const double *b,
                            const double *c)
{
    double ab0, ab1, ac0, ac1;

    // ab: vector from a to b
    ab0 = b[0] - a[0];
    ab1 = b[1] - a[1];
    // ac: vector from a to c
    ac0 = c[0] - a[0];
    ac1 = c[1] - a[1];

#ifndef THREED
    // area = norm(cross product of ab and ac) / 2
    return std::fabs(ab0*ac1 - ab1*ac0) / 2;
#else
    double ab2, ac2;
    ab2 = b[2] - a[2];
    ac2 = c[2] - a[2];

    // vector components of ab x ac
    double d0, d1, d2;
    d0 = ab1*ac2 - ab2*ac1;
    d1 = ab2*ac0 - ab0*ac2;
    d2 = ab0*ac1 - ab1*ac0;

    // area = norm(cross product of ab and ac) / 2
    return std::sqrt(d0*d0 + d1*d1 + d2*d2) / 2;
#endif
}

void compute_volume_sg(const double **coord, double &volume)
{
#ifdef USE_NPROF
    nvtxRangePushA(__FUNCTION__);
#endif
    const double *a = coord[0];
    const double *b = coord[1];
    const double *c = coord[2];
#ifdef THREED
    const double *d = coord[3];
    volume = tetrahedron_volume(a, b, c, d);
#else
    volume = triangle_area(a, b, c);
#endif
#ifdef USE_NPROF
    nvtxRangePop();
#endif

}

void compute_volume(const array_t &coord, const conn_t &connectivity,
                    double_vec &volume)
{
#ifdef USE_NPROF
    nvtxRangePushA(__FUNCTION__);
#endif
    #pragma omp parallel for default(none)      \
        shared(coord, connectivity, volume)
    for (std::size_t e=0; e<volume.size(); ++e) {
        int n0 = connectivity[e][0];
        int n1 = connectivity[e][1];
        int n2 = connectivity[e][2];

        const double *a = coord[n0];
        const double *b = coord[n1];
        const double *c = coord[n2];

#ifdef THREED
        int n3 = connectivity[e][3];
        const double *d = coord[n3];
        volume[e] = tetrahedron_volume(a, b, c, d);
#else
        volume[e] = triangle_area(a, b, c);
#endif
    }
#ifdef USE_NPROF
    nvtxRangePop();
#endif
}


void compute_dvoldt(const Variables &var, double_vec &dvoldt, elem_cache &tmp_result)
{
#ifdef USE_NPROF
    nvtxRangePushA(__FUNCTION__);
#endif
    /* dvoldt is the volumetric strain rate, weighted by the element volume,
     * lumped onto the nodes.
     */
    const double_vec& volume = *var.volume;
    const double_vec& volume_n = *var.volume_n;
    std::fill_n(dvoldt.begin(), var.nnode, 0);
/*
    class ElemFunc_dvoldt : public ElemFunc
    {
    private:
        const Variables &var;
        const double_vec &volume;
        double_vec &dvoldt;
        elem_cache &tmp_result;
        bool has_two_layers_for;
    public:
        ElemFunc_dvoldt(const Variables &var, const double_vec &volume, double_vec &dvoldt, elem_cache &tmp_result, bool has_two_layers_for) :
            var(var), volume(volume), dvoldt(dvoldt), tmp_result(tmp_result), has_two_layers_for(has_two_layers_for) {};
        void operator()(int e)
        {
            const int *conn = (*var.connectivity)[e];
            const double* strain_rate = (*var.strain_rate)[e];
            // TODO: try another definition:
            // dj = (volume[e] - volume_old[e]) / volume_old[e] / dt
            double dj = trace(strain_rate);
            double *tmp_d = tmp_result[e];
            if (has_two_layers_for) {
                for (int i=0; i<NODES_PER_ELEM; ++i)
                    tmp_d[i] = dj * volume[e];
            } else {
                for (int i=0; i<NODES_PER_ELEM; ++i) {
                    int n = conn[i];
                    #pragma omp atomic update
                    dvoldt[n] += dj * volume[e];
                }
            }
        }
    } elemf(var, volume, dvoldt, tmp_result, param.debug.has_two_layers_for);

    #pragma omp parallel for default(none) shared(var,elemf)
    for (int e=0;e<var.nelem;e++)
        elemf(e);
*/

    #pragma omp parallel for default(none) shared(var, volume, tmp_result)//, param, dvoldt)
    for (int e=0;e<var.nelem;e++) {
        const int *conn = (*var.connectivity)[e];
        const double* strain_rate = (*var.strain_rate)[e];
        // TODO: try another definition:
        // dj = (volume[e] - volume_old[e]) / volume_old[e] / dt
        double dj = trace(strain_rate);
        double *tmp_d = tmp_result[e];
//        if (param.debug.has_two_layers_for) {
        for (int i=0; i<NODES_PER_ELEM; ++i)
            tmp_d[i] = dj * volume[e];
//        } else {
//            for (int i=0; i<NODES_PER_ELEM; ++i) {
//                int n = conn[i];
//                #pragma omp atomic update
//                dvoldt[n] += dj * volume[e];
//            }
//        }
    }

//    if (param.debug.has_two_layers_for) {
    #pragma omp parallel for default(none) shared(var,dvoldt,tmp_result)
    for (int n=0;n<var.nnode;n++) {
        for( auto e = (*var.support)[n].begin(); e < (*var.support)[n].end(); ++e) {
            const int *conn = (*var.connectivity)[*e];
            for (int i=0;i<NODES_PER_ELEM;i++) {
                if (n == conn[i]) {
                    dvoldt[n] += tmp_result[*e][ i ];
                    break;
                }
            }
        }
    }
//    }

//    loop_all_elem(var.egroups, elemf);


    #pragma omp parallel for default(none)      \
        shared(var, dvoldt, volume_n)
    for (int n=0; n<var.nnode; ++n)
         dvoldt[n] /= volume_n[n];

    // std::cout << "dvoldt:\n";
    // print(std::cout, dvoldt);
    // std::cout << "\n";
#ifdef USE_NPROF
    nvtxRangePop();
#endif
}


void compute_edvoldt(const Variables &var, double_vec &dvoldt,
                     double_vec &edvoldt)
{
#ifdef USE_NPROF
    nvtxRangePushA(__FUNCTION__);
#endif
    /* edvoldt is the averaged (i.e. smoothed) dvoldt on the element.
     * It is used in update_stress() to prevent mesh locking.
     */
    #pragma omp parallel for default(none)      \
        shared(var, dvoldt, edvoldt)
    for (int e=0; e<var.nelem; ++e) {
        const int *conn = (*var.connectivity)[e];
        double dj = 0;
        for (int i=0; i<NODES_PER_ELEM; ++i) {
            int n = conn[i];
            dj += dvoldt[n];
        }
        edvoldt[e] = dj / NODES_PER_ELEM;
    }
#ifdef USE_NPROF
    nvtxRangePop();
#endif
    // std::cout << "edvoldt:\n";
    // print(std::cout, edvoldt);
    // std::cout << "\n";
}


void NMD_stress(const Variables &var, double_vec &dp_nd, tensor_t& stress)
{
    /* dp_nd is the pressure change, weighted by the element volume,
     * lumped onto the nodes.
     */
    const double_vec& volume = *var.volume;
    const double_vec& volume_n = *var.volume_n;
    std::fill_n(dp_nd.begin(), var.nnode, 0);

    class ElemFunc_NMD_stress : public ElemFunc
    {
    private:
        const Variables &var;
        const double_vec &volume;
        double_vec &dp_nd;
    public:
        ElemFunc_NMD_stress(const Variables &var, const double_vec &volume, double_vec &dp_nd) :
            var(var), volume(volume), dp_nd(dp_nd) {};
        void operator()(int e)
        {
            const int *conn = (*var.connectivity)[e];
            double dp = (*var.dpressure)[e];
            for (int i=0; i<NODES_PER_ELEM; ++i) {
                int n = conn[i];
                dp_nd[n] += dp * volume[e];
            }
        }
    } elemf(var, volume, dp_nd);

    loop_all_elem(var.egroups, elemf);

    #pragma omp parallel for default(none)      \
        shared(var, dp_nd, volume_n)
    for (int n=0; n<var.nnode; ++n)
         dp_nd[n] /= volume_n[n];


    /* dp_el is the averaged (i.e. smoothed) dp_nd on the element.
     */
    #pragma omp parallel for default(none)      \
        shared(var, dp_nd, stress)
    for (int e=0; e<var.nelem; ++e) {
        const int *conn = (*var.connectivity)[e];
        double dp = 0;
        for (int i=0; i<NODES_PER_ELEM; ++i) {
            int n = conn[i];
            dp += dp_nd[n];
        }
        double dp_el = dp / NODES_PER_ELEM;

    	double* s = stress[e];
	double dp_orig = (*var.dpressure)[e];
	for (int i=0; i<NDIMS; ++i) s[i] += ( - dp_orig + dp_el ) / NDIMS;
    }
}


double compute_dt(const Param& param, const Variables& var)
{
#ifdef USE_NPROF
    nvtxRangePushA(__FUNCTION__);
#endif
    // constant dt
    if (param.control.fixed_dt != 0) return param.control.fixed_dt;

    // dynamic dt
    const int nelem = var.nelem;
    const conn_t& connectivity = *var.connectivity;
    const array_t& coord = *var.coord;
    const double_vec& volume = *var.volume;

    double dt_maxwell = std::numeric_limits<double>::max();
    double dt_diffusion = std::numeric_limits<double>::max();
    double minl = std::numeric_limits<double>::max();

//    #pragma omp parallel for reduction(min:minl,dt_maxwell,dt_diffusion) default(none) shared(param,var, nelem, connectivity, coord, volume)
    for (int e=0; e<nelem; ++e) {
        int n0 = connectivity[e][0];
        int n1 = connectivity[e][1];
        int n2 = connectivity[e][2];

        const double *a = coord[n0];
        const double *b = coord[n1];
        const double *c = coord[n2];

        // min height of this element
        double minh;
#ifdef THREED
        {
            int n3 = connectivity[e][3];
            const double *d = coord[n3];

            // max facet area of this tet
            double maxa = std::max(std::max(triangle_area(a, b, c),
                                            triangle_area(a, b, d)),
                                   std::max(triangle_area(c, d, a),
                                            triangle_area(c, d, b)));
            minh = 3 * volume[e] / maxa;
        }
#else
        {
            // max edge length of this triangle
            double maxl = std::sqrt(std::max(std::max(dist2(a, b),
                                                      dist2(b, c)),
                                             dist2(a, c)));
            minh = 2 * volume[e] / maxl;
        }
#endif
        dt_maxwell = std::min(dt_maxwell,
                              0.5 * var.mat->visc_min / (1e-40 + var.mat->shearm(e)));
        if (param.control.has_thermal_diffusion)
            dt_diffusion = std::min(dt_diffusion,
                                    0.5 * minh * minh / var.mat->therm_diff_max);
	    minl = std::min(minl, minh);
    }

    double max_vbc_val;
    if (param.control.characteristic_speed == 0)
        max_vbc_val = find_max_vbc(param.bc, *var.vbc_period_ratio_x);
    else
        max_vbc_val = param.control.characteristic_speed;

    double dt_advection = 0.5 * minl / max_vbc_val;
    double dt_elastic = (param.control.is_quasi_static) ?
        0.5 * minl / (max_vbc_val * param.control.inertial_scaling) :
        0.5 * minl / std::sqrt(var.mat->bulkm(0) / var.mat->rho(0));
    double dt = std::min(std::min(dt_elastic, dt_maxwell),
                         std::min(dt_advection, dt_diffusion)) * param.control.dt_fraction;
    if (param.debug.dt) {
        std::cout << "step #" << var.steps << "  dt: " << dt_maxwell << " " << dt_diffusion
                  << " " << dt_advection << " " << dt_elastic << " sec\n";
    }
    if (dt <= 0) {
        std::cerr << "Error: dt <= 0!  " << dt_maxwell << " " << dt_diffusion
                  << " " << dt_advection << " " << dt_elastic << "\n";
        std::exit(11);
    }
#ifdef USE_NPROF
    nvtxRangePop();
#endif
    return dt;
}


void compute_mass(const Param &param,
                  const int_vec &egroups, const Variables &var,
                  double max_vbc_val, double_vec &volume_n,
                  double_vec &mass, double_vec &tmass, elem_cache &tmp_result)
{
#ifdef USE_NPROF
    nvtxRangePushA(__FUNCTION__);
#endif
    // volume_n is (node-averaged volume * NODES_PER_ELEM)
    volume_n.assign(volume_n.size(), 0);
    mass.assign(mass.size(), 0);
    tmass.assign(tmass.size(), 0);

    const double pseudo_speed = max_vbc_val * param.control.inertial_scaling;
/*
    class ElemFunc_mass : public ElemFunc
    {
    private:
        const MatProps &mat;
        const conn_t &connectivity;
        const double_vec &volume;
        double_vec &volume_n;
        double_vec &mass;
        double_vec &tmass;
        elem_cache &tmp_result;
        double pseudo_speed;
        bool is_quasi_static;
        bool has_thermal_diffusion;
        bool has_two_layers_for;
    public:
        ElemFunc_mass(const MatProps &mat, const conn_t &connectivity, const double_vec &volume,
                      double pseudo_speed, bool is_quasi_static, bool has_thermal_diffusion, bool has_two_layers_for,
                      double_vec &volume_n, double_vec &mass, double_vec &tmass, elem_cache &tmp_result) :
            mat(mat), connectivity(connectivity), volume(volume),
            volume_n(volume_n), mass(mass), tmass(tmass), tmp_result(tmp_result),
            pseudo_speed(pseudo_speed), is_quasi_static(is_quasi_static),
            has_thermal_diffusion(has_thermal_diffusion), has_two_layers_for(has_two_layers_for) {};
        void operator()(int e)
        {
            double rho = (is_quasi_static) ?
                mat.bulkm(e) / (pseudo_speed * pseudo_speed) :  // pseudo density for quasi-static sim
                mat.rho(e);                                     // true density for dynamic sim
            double m = rho * volume[e] / NODES_PER_ELEM;
            double tm = mat.rho(e) * mat.cp(e) * volume[e] / NODES_PER_ELEM;
            const int *conn = connectivity[e];

            double *tmp_v = tmp_result[e];
            for (int i=0; i<NODES_PER_ELEM; ++i) {
                if (has_two_layers_for) {
                    tmp_v[i] = volume[e];
                    tmp_v[i+NODES_PER_ELEM] = m;
                    if (has_thermal_diffusion) {
                        tmp_v[i+NODES_PER_ELEM*2] = tm;
                    }
                } else {
                    #pragma omp atomic update
                    volume_n[conn[i]] += volume[e];
                    #pragma omp atomic update
                    mass[conn[i]] += m;
                    if (has_thermal_diffusion) {
                        #pragma omp atomic update
                        tmass[conn[i]] += tm;
                    }
                }
            }
        }
    } elemf(*var.mat, *var.connectivity, *var.volume, pseudo_speed, param.control.is_quasi_static,
            param.control.has_thermal_diffusion, param.debug.has_two_layers_for, volume_n, mass, tmass, tmp_result);

    #pragma omp parallel for default(none) shared(var,elemf)
    for (int e=0;e<var.nelem;e++)
        elemf(e);
*/

    #pragma omp parallel for default(none) shared(var, param, tmp_result)//, volume_n, mass, tmass)
    for (int e=0;e<var.nelem;e++) {
        double rho = (param.control.is_quasi_static) ?
            (*var.mat).bulkm(e) / (pseudo_speed * pseudo_speed) :  // pseudo density for quasi-static sim
            (*var.mat).rho(e);                                     // true density for dynamic sim
        double m = rho * (*var.volume)[e] / NODES_PER_ELEM;
        double tm = (*var.mat).rho(e) * (*var.mat).cp(e) * (*var.volume)[e] / NODES_PER_ELEM;
        const int *conn = (*var.connectivity)[e];

        double *tmp_v = tmp_result[e];
        for (int i=0; i<NODES_PER_ELEM; ++i) {
//            if (param.debug.has_two_layers_for) {
            tmp_v[i] = (*var.volume)[e];
            tmp_v[i+NODES_PER_ELEM] = m;
            if (param.control.has_thermal_diffusion)
                tmp_v[i+NODES_PER_ELEM*2] = tm;
//            } else {
//                #pragma omp atomic update
//                volume_n[conn[i]] += (*var.volume)[e];
//                #pragma omp atomic update
//                mass[conn[i]] += m;
//                if (param.control.has_thermal_diffusion) {
//                    #pragma omp atomic update
//                    tmass[conn[i]] += tm;
//                }
//            }
        }
    }

//    if (param.debug.has_two_layers_for) {
    #pragma omp parallel for default(none) shared(param,var,volume_n,mass,tmass,tmp_result)
    for (int n=0;n<var.nnode;n++) {
        for( auto e = (*var.support)[n].begin(); e < (*var.support)[n].end(); ++e) {
            const int *conn = (*var.connectivity)[*e];
            for (int i=0;i<NODES_PER_ELEM;i++) {
                if (n == conn[i]) {
                    volume_n[n] += tmp_result[*e][ i ];
                    mass[n] += tmp_result[*e][ i + NODES_PER_ELEM ];
                    if (param.control.has_thermal_diffusion)
                        tmass[n] += tmp_result[*e][ i + NODES_PER_ELEM*2 ];
                    break;
                }
            }
        }
    }
//    }
#ifdef USE_NPROF
    nvtxRangePop();
#endif

//    loop_all_elem(egroups, elemf);
}


void compute_shape_fn(const Variables &var, const int_vec &egroups,
                      shapefn &shpdx, shapefn &shpdy, shapefn &shpdz)
{
#ifdef USE_NPROF
    nvtxRangePushA(__FUNCTION__);
#endif
/*
    class ElemFunc_shape_fn : public ElemFunc
    {
    private:
        const array_t &coord;
        const conn_t &connectivity;
        const double_vec &volume;
        shapefn &shpdx, &shpdy, &shpdz;
    public:
        ElemFunc_shape_fn(const array_t &coord, const conn_t &connectivity, const double_vec &volume,
                          shapefn &shpdx, shapefn &shpdy, shapefn &shpdz) :
            coord(coord), connectivity(connectivity), volume(volume),
            shpdx(shpdx), shpdy(shpdy), shpdz(shpdz) {};
        void operator()(int e)
        {

            int n0 = connectivity[e][0];
            int n1 = connectivity[e][1];
            int n2 = connectivity[e][2];

            const double *d0 = coord[n0];
            const double *d1 = coord[n1];
            const double *d2 = coord[n2];

#ifdef THREED
            {
                int n3 = connectivity[e][3];
                const double *d3 = coord[n3];

                double iv = 1 / (6 * volume[e]);

                double x01 = d0[0] - d1[0];
                double x02 = d0[0] - d2[0];
                double x03 = d0[0] - d3[0];
                double x12 = d1[0] - d2[0];
                double x13 = d1[0] - d3[0];
                double x23 = d2[0] - d3[0];

                double y01 = d0[1] - d1[1];
                double y02 = d0[1] - d2[1];
                double y03 = d0[1] - d3[1];
                double y12 = d1[1] - d2[1];
                double y13 = d1[1] - d3[1];
                double y23 = d2[1] - d3[1];

                double z01 = d0[2] - d1[2];
                double z02 = d0[2] - d2[2];
                double z03 = d0[2] - d3[2];
                double z12 = d1[2] - d2[2];
                double z13 = d1[2] - d3[2];
                double z23 = d2[2] - d3[2];

                shpdx[e][0] = iv * (y13*z12 - y12*z13);
                shpdx[e][1] = iv * (y02*z23 - y23*z02);
                shpdx[e][2] = iv * (y13*z03 - y03*z13);
                shpdx[e][3] = iv * (y01*z02 - y02*z01);

                shpdy[e][0] = iv * (z13*x12 - z12*x13);
                shpdy[e][1] = iv * (z02*x23 - z23*x02);
                shpdy[e][2] = iv * (z13*x03 - z03*x13);
                shpdy[e][3] = iv * (z01*x02 - z02*x01);

                shpdz[e][0] = iv * (x13*y12 - x12*y13);
                shpdz[e][1] = iv * (x02*y23 - x23*y02);
                shpdz[e][2] = iv * (x13*y03 - x03*y13);
                shpdz[e][3] = iv * (x01*y02 - x02*y01);
            }
#else
            {
                double iv = 1 / (2 * volume[e]);

                shpdx[e][0] = iv * (d1[1] - d2[1]);
                shpdx[e][1] = iv * (d2[1] - d0[1]);
                shpdx[e][2] = iv * (d0[1] - d1[1]);

                shpdz[e][0] = iv * (d2[0] - d1[0]);
                shpdz[e][1] = iv * (d0[0] - d2[0]);
                shpdz[e][2] = iv * (d1[0] - d0[0]);
            }
#endif
        }
    } elemf(*var.coord, *var.connectivity, *var.volume, shpdx, shpdy, shpdz);

    #pragma omp parallel for default(none) shared(var,elemf)
    for (int e=0;e<var.nelem;e++)
        elemf(e);
*/

    #pragma omp parallel for default(none) shared(var, shpdx, shpdy, shpdz)
    for (int e=0;e<var.nelem;e++) {

        int n0 = (*var.connectivity)[e][0];
        int n1 = (*var.connectivity)[e][1];
        int n2 = (*var.connectivity)[e][2];

        const double *d0 = (*var.coord)[n0];
        const double *d1 = (*var.coord)[n1];
        const double *d2 = (*var.coord)[n2];

#ifdef THREED
        {
            int n3 = (*var.connectivity)[e][3];
            const double *d3 = (*var.coord)[n3];

            double iv = 1 / (6 * (*var.volume)[e]);

            double x01 = d0[0] - d1[0];
            double x02 = d0[0] - d2[0];
            double x03 = d0[0] - d3[0];
            double x12 = d1[0] - d2[0];
            double x13 = d1[0] - d3[0];
            double x23 = d2[0] - d3[0];

            double y01 = d0[1] - d1[1];
            double y02 = d0[1] - d2[1];
            double y03 = d0[1] - d3[1];
            double y12 = d1[1] - d2[1];
            double y13 = d1[1] - d3[1];
            double y23 = d2[1] - d3[1];

            double z01 = d0[2] - d1[2];
            double z02 = d0[2] - d2[2];
            double z03 = d0[2] - d3[2];
            double z12 = d1[2] - d2[2];
            double z13 = d1[2] - d3[2];
            double z23 = d2[2] - d3[2];

            shpdx[e][0] = iv * (y13*z12 - y12*z13);
            shpdx[e][1] = iv * (y02*z23 - y23*z02);
            shpdx[e][2] = iv * (y13*z03 - y03*z13);
            shpdx[e][3] = iv * (y01*z02 - y02*z01);

            shpdy[e][0] = iv * (z13*x12 - z12*x13);
            shpdy[e][1] = iv * (z02*x23 - z23*x02);
            shpdy[e][2] = iv * (z13*x03 - z03*x13);
            shpdy[e][3] = iv * (z01*x02 - z02*x01);

            shpdz[e][0] = iv * (x13*y12 - x12*y13);
            shpdz[e][1] = iv * (x02*y23 - x23*y02);
            shpdz[e][2] = iv * (x13*y03 - x03*y13);
            shpdz[e][3] = iv * (x01*y02 - x02*y01);
        }
#else
        {
            double iv = 1 / (2 * (*var.volume)[e]);

            shpdx[e][0] = iv * (d1[1] - d2[1]);
            shpdx[e][1] = iv * (d2[1] - d0[1]);
            shpdx[e][2] = iv * (d0[1] - d1[1]);

            shpdz[e][0] = iv * (d2[0] - d1[0]);
            shpdz[e][1] = iv * (d0[0] - d2[0]);
            shpdz[e][2] = iv * (d1[0] - d0[0]);
        }
    }
#endif


//    loop_all_elem(egroups, elemf);
#ifdef USE_NPROF
    nvtxRangePop();
#endif
}


double elem_quality(const array_t &coord, const conn_t &connectivity,
                    const double_vec &volume, int e)
{
    /* This function returns the quality (0~1) of the element.
     * The quality of an equidistant (i.e. best quality) tetrahedron/triangle is 1.
     */
    double quality;
    double vol = volume[e];
    int n0 = connectivity[e][0];
    int n1 = connectivity[e][1];
    int n2 = connectivity[e][2];

    const double *a = coord[n0];
    const double *b = coord[n1];
    const double *c = coord[n2];

#ifdef THREED
    {
        int n3 = connectivity[e][3];
        const double *d = coord[n3];
        double normalization_factor = 216 * std::sqrt(3);

        double area_sum = (triangle_area(a, b, c) +
                           triangle_area(a, b, d) +
                           triangle_area(c, d, a) +
                           triangle_area(c, d, b));
        quality = normalization_factor * vol * vol / (area_sum * area_sum * area_sum);
    }
#else
    {
        double normalization_factor = 4 * std::sqrt(3);

        double dist2_sum = dist2(a, b) + dist2(b, c) + dist2(a, c);
        quality = normalization_factor * vol / dist2_sum;
    }
#endif

    return quality;
}


double worst_elem_quality(const array_t &coord, const conn_t &connectivity,
                          const double_vec &volume, int &worst_elem)
{
    double q = 1;
    worst_elem = 0;
    for (std::size_t e=0; e<volume.size(); e++) {
        double quality = elem_quality(coord, connectivity, volume, e);
        if (quality < q) {
            q = quality;
            worst_elem = e;
        }
    }
    return q;
}


