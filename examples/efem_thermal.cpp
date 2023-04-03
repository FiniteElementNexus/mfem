//                              MFEM Example 35
//
//
// Compile with: make ex35
//
// Sample runs:
//     ex35 -alpha 10
//     ex35 -lambda 0.1 -mu 0.1
//     ex35 -r 5 -o 2 -alpha 5.0 -epsilon 0.01 -mi 50 -mf 0.5 -tol 1e-5
//     ex35 -r 6 -o 1 -alpha 10.0 -epsilon 0.01 -mi 50 -mf 0.5 -tol 1e-5
//
//
// Description: This example code demonstrates the use of MFEM to solve a
//              density-filtered [3] topology optimization problem. The
//              objective is to minimize the thermal compliance
//
//                  minimize ∫_Ω f u dx over u ∈ H¹(Ω) and ρ ∈ L²(Ω)
//
//                  subject to
//
//                    -∇⋅(r(ρ̃)∇ u) = f       in Ω + BCs
//                    -ϵ²Δρ̃ + ρ̃ = ρ          in Ω + Neumann BCs
//                    0 ≤ ρ ≤ 1              in Ω
//                    u ≤ 1                  in Ω
//                    ∫_Ω ρ dx = θ vol(Ω)
//
//              Here, r(ρ̃) = ρ₀ + ρ̃³ (1-ρ₀) is the solid isotropic material
//              penalization (SIMP) law, ϵ > 0 is the design length scale,
//              and 0 < θ < 1 is the volume fraction. Note that we have
//
//              More specifically, we have f = 0 in an insulated rectagular
//              domain Ω = (0, 1) x (0, 1) where the left middle section
//              {x = 0} x (0.4, 0.6) is held at temperature 0.
//
//                                INSULATED
//                       ---------------------------  1
//                       |                         |
//                       |                         |
//                       * -                       |
//                 u = 0 * |  0.2                  |
//                       * -                       |
//                       |                         |
//                       |                         |
//                       ---------------------------   0
//                       0                         1
//
//              The problem is discretized and gradients are computing using
//              finite elements [1]. The design is optimized using an entropic
//              mirror descent algorithm introduced by Keith and Surowiec [2]
//              that is tailored to the bound constraint 0 ≤ ρ ≤ 1.
//
//              This example highlights the ability of MFEM to deliver high-
//              order solutions to inverse design problems and showcases how
//              to set up and solve PDE-constrained optimization problems
//              using the so-called reduced space approach.
//
//
// [1] Andreassen, E., Clausen, A., Schevenels, M., Lazarov, B. S., & Sigmund, O.
//    (2011). Efficient topology optimization in MATLAB using 88 lines of
//    code. Structural and Multidisciplinary Optimization, 43(1), 1-16.
// [2] Keith, B. and Surowiec, T. (2023) The entropic finite element method
//     (in preparation).
// [3] Lazarov, B. S., & Sigmund, O. (2011). Filters in topology optimization
//     based on Helmholtz‐type differential equations. International Journal
//     for Numerical Methods in Engineering, 86(6), 765-781.

#include "mfem.hpp"
#include <iostream>
#include <fstream>
#include "efem.hpp"

using namespace std;
using namespace mfem;
/**
 * ---------------------------------------------------------------
 *                      ALGORITHM PREAMBLE
 * ---------------------------------------------------------------
 *
 *  The Lagrangian for this problem is
 *
 *          L(u,ρ,ρ̃,w,w̃) = (f,u) + (r(ρ̃)∇u, ∇w) - (f,w) + ϵ^2(∇ρ̃, ∇w̃) + (ρ̃ - ρ, w̃)
 *                       + α⁻¹D≤(u, uk) + α⁻¹(D≥(ρ, ρk) + D≤(ρ, ρk))
 *
 *  where
 *
 *    r(ρ̃) = ρ₀ + ρ̃³ (1 - ρ₀)            (SIMP rule)
 *
 *    D≥(x, y) = ∫ xlog(x/y) - (x - y)   (Lower Bound, away from 0)
 *
 *    D≤(x, y) = D≥(1 - x, 1 - y)        (Upper Bound, away from 1)
 *
 * ---------------------------------------------------------------
 *
 *  Discretization choices:
 *
 *     u ∈ Vh ⊂ H¹ (order p)
 *     w ∈ Vh ⊂ H¹ (order p)
 *     ρ̃ ∈ Vl ⊂ H¹ (order p)
 *     w̃ ∈ Vl ⊂ H¹ (order p)
 *     ψ ∈ Wl ⊂ L² (order p - 1)
 *
 *  where ρ = sigmoid(ψ) so that 0≤ρ≤1 is strongly enforced
 *
 * ---------------------------------------------------------------
 *                          ALGORITHM
 * ---------------------------------------------------------------
 *
 *  Update ψ with projected mirror descent via the following algorithm.
 *
 *  0. Initialize density field ψ = sigmoid⁻¹(θ) so that ∫ρ = ∫sigmoid(ψ) = θ|Ω|
 *
 *  While not converged:
 *
 *     1. Solve filter equation ∂_w̃ L = 0; i.e.,
 *
 *           (ϵ² ∇ ρ̃, ∇ v ) + (ρ̃,v) = (ρ,v)   ∀ v ∈ Vl.
 *
 *     2. Solve primal problem ∂_w L = 0; i.e.,
 *
 *                    (r(ρ̃) ∇u, ∇v) = (f,v)   ∀ v ∈ Vh.
 *
 *     3. Solve dual problem ∂_u L = 0; i.e.,
 *
 *                    (r(ρ̃) ∇w, ∇v) = (f,v) + α⁻¹(log(u/uk), v)    ∀ v ∈ Vh.
 *
 *        NOTE: When there is no constraint u≤1, then w = u.
 *              In that case, we do not have to solve the dual problem.
 *
 *     4. Solve for filtered gradient ∂_ρ̃ L = 0; i.e.,
 *
 *      (ϵ² ∇ w̃ , ∇ v ) + (w̃ ,v) = ( r'(ρ̃) (∇ u ⋅ ∇ w), v)   ∀ v ∈ Vl.
 *
 *     5. Set intermediate variable ψ⋆ = ψ - α⁻¹ w̃.
 *
 *     6. Update ψ by ψ = proj(ψ⋆) = ψ⋆ + c where c is chosen to be
 *
 *                ∫ sigmoid(ψ⋆ + c) = θ|Ω|.
 *
 *  end
 *
 */


/**
 * @brief alpha*(log(max(a, tol)) - log(max(b, tol)))
 *
 */
class SafeLogDiffGridFunctionCoefficient : public
   SafeLogarithmicGridFunctionCoefficient
{
private:
   SafeLogarithmicGridFunctionCoefficient
   *gf_other; // gridfunction log(b) to be subtracted
   double a = 1.0;

public:

   /**
    * @brief log(max(a, tol)) - log(max(b, tol))
    *
    */
   SafeLogDiffGridFunctionCoefficient(GridFunction *self_gf,
                                      GridFunction *other_gf, const double tolerance):
      SafeLogarithmicGridFunctionCoefficient(self_gf, tolerance),
      gf_other(new SafeLogarithmicGridFunctionCoefficient(other_gf, tolerance)) {}

   /// Evaluate the coefficient at @a ip.
   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip)
   {
      return a*(SafeLogarithmicGridFunctionCoefficient::Eval(T, ip)
                - gf_other->Eval(T, ip));
   }
   void SetAlpha(const double alpha) { a = alpha; }
};


/**
 * @brief log(max(a, tol)) - log(max(b, tol))
 *
 */
class SigmoidDiffGridFunctionCoefficient : public
   SigmoidGridFunctionCoefficient
{
private:
   SigmoidGridFunctionCoefficient
   *gf_other; // gridfunction log(b) to be subtracted

public:

   /**
    * @brief log(max(a, tol)) - log(max(b, tol))
    *
    */
   SigmoidDiffGridFunctionCoefficient(GridFunction *a,
                                      GridFunction *b):
      SigmoidGridFunctionCoefficient(a),
      gf_other(new SigmoidGridFunctionCoefficient(b)) {}

   /// Evaluate the coefficient at @a ip.
   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip)
   {
      return SigmoidGridFunctionCoefficient::Eval(T, ip)
             - gf_other->Eval(T, ip);
   }
};

/**
 * @brief -r'(ρ̃)(∇ u ⋅ ∇ w)
 *
 */
class SIMPDerEnergyCoefficient : public GridFunctionCoefficient
{
private:
   SIMPDerCoefficient *r_prime_rho;
   GradientGridFunctionCoefficient *gradu;
   GradientGridFunctionCoefficient *gradw;
   Vector gradu_val, gradw_val;
public:

   SIMPDerEnergyCoefficient(GridFunction *rho_filter, const double exponent,
                            const double rho_min, GridFunction *u,
                            GridFunction *w):GridFunctionCoefficient()
   {
      r_prime_rho = new SIMPDerCoefficient(rho_filter, exponent, rho_min);
      gradu = new GradientGridFunctionCoefficient(u);
      gradw = new GradientGridFunctionCoefficient(w);
      const int dim = u->FESpace()->GetMesh()->Dimension();
      gradu_val = Vector(dim);
      gradw_val = Vector(dim);
   }

   /// Evaluate the coefficient at @a ip.
   virtual double Eval(ElementTransformation &T,
                       const IntegrationPoint &ip)
   {
      gradu->Eval(gradu_val, T, ip);
      gradw->Eval(gradw_val, T, ip);
      return -r_prime_rho->Eval(T, ip)*(gradu_val*gradw_val);
   }
};

inline void clip(GridFunction &gf, const double lower, const double upper)
{
   for (auto &x : gf)
   {
      x = min(max(x, lower), upper);
   }
}

int main(int argc, char *argv[])
{

   // 0 - 1. Parse command-line options.
   int ref_levels = 4; // The number of initial mesh refinement
   int order = 2; // Polynomial order p. State - p, Design - p - 1, Filter - p
   bool visualization = true;
   double alpha0 = 1.0; // Update rule
   double epsilon = 0.01; // Design parameter, ϵ.
   double mass_fraction = 0.3; // mass fraction, θ.
   int max_it = 1e2; // projected mirror gradient maximum iteration
   double tol = 1e-4; // Projected mirror gradient tolerance
   double rho_min = 1e-6; // SIMP ρ0
   double exponent = 3; // SIMP exponent

   OptionsParser args(argc, argv);
   args.AddOption(&ref_levels, "-r", "--refine",
                  "Number of times to refine the mesh uniformly.");
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&alpha0, "-alpha", "--alpha-step-length",
                  "Step length for gradient descent.");
   args.AddOption(&epsilon, "-epsilon", "--epsilon-thickness",
                  "epsilon phase field thickness");
   args.AddOption(&max_it, "-mi", "--max-it",
                  "Maximum number of gradient descent iterations.");
   args.AddOption(&tol, "-tol", "--tol",
                  "Exit tolerance for ρ ");
   args.AddOption(&mass_fraction, "-mf", "--mass-fraction",
                  "Mass fraction for diffusion coefficient.");
   args.AddOption(&rho_min, "-rmin", "--rho-min",
                  "Minimum of density coefficient.");
   args.AddOption(&exponent, "-exp", "--exponent",
                  "SIMP exponent.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.Parse();
   if (!args.Good())
   {
      args.PrintUsage(cout);
      return 1;
   }
   args.PrintOptions(cout);

   Mesh mesh = Mesh::MakeCartesian2D(10,10,mfem::Element::Type::QUADRILATERAL,true,
                                     1.0,1.0);

   int dim = mesh.Dimension();

   // 2. Set BCs.
   for (int i = 0; i<mesh.GetNBE(); i++)
   {
      Element * be = mesh.GetBdrElement(i);
      Array<int> vertices;
      be->GetVertices(vertices);

      double * coords1 = mesh.GetVertex(vertices[0]);
      double * coords2 = mesh.GetVertex(vertices[1]);

      Vector center(2);
      center(0) = 0.5*(coords1[0] + coords2[0]);
      center(1) = 0.5*(coords1[1] + coords2[1]);

      // if (abs(center(1) - 0.5) < 0.1 && center(0) < 1e-12)
      if (center(0) < 1e-12 // left
          && std::abs(center(1) - 0.5) < 0.1 // middle
         )
      {
         // the left center
         be->SetAttribute(1);
      }
      else
      {
         // all other boundaries
         be->SetAttribute(2);
      }
   }
   mesh.SetAttributes();

   // 3. Refine the mesh.
   for (int lev = 0; lev < ref_levels; lev++)
   {
      mesh.UniformRefinement();
   }

   const int filter_order = order - 1;


   // 4. Define the necessary finite element spaces on the mesh.
   H1_FECollection state_fec(order, dim); // FE collection for u
   H1_FECollection filter_fec(order-1, dim); // FE collection for ρ̃
   L2_FECollection control_fec(order-1, dim); // FE collection for ρ
   FiniteElementSpace state_fes(&mesh, &state_fec); // Space for u
   FiniteElementSpace filter_fes(&mesh, &filter_fec); // space for ρ̃
   FiniteElementSpace control_fes(&mesh, &control_fec); // space for ρ

   int state_size = state_fes.GetTrueVSize();
   int control_size = control_fes.GetTrueVSize();
   int filter_size = filter_fes.GetTrueVSize();
   cout << "Number of state unknowns: " << state_size << endl;
   cout << "Number of filter unknowns: " << filter_size << endl;
   cout << "Number of control unknowns: " << control_size << endl;

   // 5. Set the initial guess for ρ.
   GridFunction u(&state_fes);
   GridFunction w(&state_fes);
   GridFunction psi(&control_fes);
   GridFunction rho_filter(&filter_fes);
   GridFunction w_filter(&filter_fes);

   SigmoidGridFunctionCoefficient rho(&psi); // sigmoid(ρ)

   u = 0.0;
   w = 0.0;
   w_filter = 0.0;
   psi = invsigmoid(mass_fraction);
   rho_filter.ProjectCoefficient(rho);

   GridFunction u_old(u);
   GridFunction psi_old(psi);
   ConstantCoefficient one(1.0);
   ConstantCoefficient zero(0.0);

   // 6. Set-up the physics solver.

   // 6 - 1. State problem LHS
   Array<int> ess_bdr_state(mesh.bdr_attributes.Max()); // Dirichlet at bdr == 1
   // Only the first component is essential bdr
   ess_bdr_state = 0;
   ess_bdr_state[0] = 1;
   // r(ρ̃) = ρ0 + (1-ρ0)ρ̃^p
   SIMPCoefficient r_rho_filter(&rho_filter, exponent, rho_min);
   // heat source
   ConstantCoefficient f(1.0);
   // (r(ρ̃)∇ u, ∇ v)
   DiffusionSolver *state_solver = new DiffusionSolver();
   state_solver->SetMesh(&mesh);
   state_solver->SetFESpace(&state_fes);
   state_solver->SetEssentialBoundary(ess_bdr_state);
   state_solver->SetDiffusionCoefficient(&r_rho_filter);
   state_solver->SetupFEM();

   // 6 - 3. Filter problem LHS
   Array<int> ess_bdr_filter(mesh.bdr_attributes.Max()); // Pure Neumann
   ess_bdr_filter = 0;
   // ϵ^2, filter diffusion coeff
   ConstantCoefficient eps_squared(epsilon*epsilon);
   // (ϵ∇ ρ̃, ∇ v) + (ρ̃, v)
   DiffusionSolver *filter_solver = new DiffusionSolver();
   filter_solver->SetMesh(&mesh);
   filter_solver->SetFESpace(&filter_fes);
   filter_solver->SetEssentialBoundary(ess_bdr_filter);
   filter_solver->SetDiffusionCoefficient(&eps_squared);
   filter_solver->SetMassCoefficient(&one);
   filter_solver->SetupFEM();
   SIMPDerEnergyCoefficient r_energy(&rho_filter, exponent, rho_min, &u, &w);

   // 6 - 5. Prepare for Projection
   LinearForm volForm(&control_fes);
   volForm.AddDomainIntegrator(new DomainLFIntegrator(one, 0, 0));
   volForm.Assemble();
   const double vol = volForm.Sum(); // domain volume
   out << "|Ω| = " << vol << std::endl;
   SigmoidDensityProjector volProj(&control_fes, mass_fraction, vol);

   // 6 - 6. M⁻¹: Vl -> Wl
   BilinearForm invMass(&control_fes);
   invMass.AddDomainIntegrator(new InverseIntegrator(new MassIntegrator()));
   invMass.Assemble();
   SparseMatrix invM;
   Array<int> empty;
   invMass.FormSystemMatrix(empty,invM);
   GridFunctionCoefficient w_filter_cf(&w_filter);
   LinearForm w_filter_load(&control_fes);
   w_filter_load.AddDomainIntegrator(new DomainLFIntegrator(w_filter_cf));

   // 10. Connect to GLVis. Prepare for VisIt output.
   char vishost[] = "localhost";
   int  visport   = 19916;
   socketstream sout_u,sout_r,sout_rho;
   if (visualization)
   {
      sout_u.open(vishost, visport);
      sout_rho.open(vishost, visport);
      sout_r.open(vishost, visport);
      sout_u.precision(8);
      sout_rho.precision(8);
      sout_r.precision(8);

      sout_u << "solution\n" << mesh << u;
      sout_u << "view 0 0\n";  // view from top
      sout_u << "keys jl********\n";  // turn off perspective and light
      sout_u << "window_title 'Temperature u'";
      sout_u.flush();

      GridFunction rho_gf(&control_fes);
      rho_gf.ProjectCoefficient(rho);

      sout_rho << "solution\n" << mesh << rho_gf;
      sout_rho << "view 0 0\n";  // view from top
      sout_rho << "keys jl********\n";  // turn off perspective and light
      sout_rho << "window_title 'Density ρ'";
      sout_rho.flush();

      sout_r << "solution\n" << mesh << rho_filter;
      sout_r << "view 0 0\n";  // view from top
      sout_r << "keys jl********\n";  // turn off perspective and light
      sout_r << "window_title 'Filtered density ρ̃'";
      sout_r.flush();
   }

   // mfem::ParaViewDataCollection paraview_dc("Elastic_compliance", &mesh);
   // paraview_dc.SetPrefixPath("ParaView");
   // paraview_dc.SetLevelsOfDetail(order);
   // paraview_dc.SetCycle(0);
   // paraview_dc.SetDataFormat(VTKFormat::BINARY);
   // paraview_dc.SetHighOrderOutput(true);
   // paraview_dc.SetTime(0.0);
   // paraview_dc.RegisterField("displacement",&u);
   // paraview_dc.RegisterField("density",&rho);
   // paraview_dc.RegisterField("filtered_density",&rho_filter);

   // 11. Iterate
   double c0 = 0.0;
   SigmoidDiffGridFunctionCoefficient succ_err(&psi, &psi_old);
   GridFunction zero_gf(&control_fes);
   zero_gf = 0.0;
   for (int k = 1; k < max_it; k++)
   {
      const double alpha = alpha0*k;

      cout << "\nStep = " << k << endl;

      // Step 1 - Filter solve
      mfem::out << "(ϵ^2 ∇ ρ̃, ∇ v) + (ρ̃,v) = (ρ,v)" << std::endl;
      filter_solver->SetRHSCoefficient(&rho);
      filter_solver->Solve(&rho_filter);

      // Step 2 - Primal solve
      mfem::out << "(r(ρ̃) ∇ u, ∇ v) = (f, v)" << std::endl;
      state_solver->SetRHSCoefficient(&f);
      state_solver->Solve(&u);

      // Step 3 - Dual solve
      // @note w is actually -w as we do not negate the RHS.
      mfem::out << "(r(ρ̃) ∇ w, ∇ v) = (f, v) + α⁻¹(log(u/uk), v)" <<
                std::endl;
      state_solver->SetRHSCoefficient(&f);
      w = u;
      state_solver->Solve(&w);

      // Step 4 - Dual filter solve
      // @note Because of Step 3, we also solving -w̃ instead of w̃.
      mfem::out <<
                "(ϵ^2 ∇ w̃, ∇ v) + (w̃, v) = (r'(ρ̃)(∇ u ⋅ ∇ w), v)" <<
                std::endl;
      filter_solver->SetRHSCoefficient(&r_energy);
      filter_solver->Solve(&w_filter);

      // Step 5 - Get ψ⋆ = ψ - α⁻¹ w̃
      w_filter_load.Assemble();
      psi_old = psi;
      invM.AddMult(w_filter_load, psi, 1/alpha);

      // Step 6 - ψ = proj(ψ⋆)
      // bound psi so that 0≈sigmoid(-100) < rho < sigmoid(100)≈1
      // project
      clip(psi, -100.0, 100.0);
      const double currVol = volProj.Apply(psi, 20);

      if (visualization)
      {

         GridFunction rho_gf(&state_fes); // use continuous fes for visualization
         rho_gf.ProjectCoefficient(rho);
         sout_rho << "solution\n" << mesh << rho_gf;
         sout_rho.flush();

         sout_r << "solution\n" << mesh << rho_filter;
         sout_r.flush();

         sout_u << "solution\n" << mesh << u;
         sout_u.flush();

         // paraview_dc.SetCycle(k);
         // paraview_dc.SetTime((double)k);
         // paraview_dc.Save();
      }
      const double norm_reduced_gradient = zero_gf.ComputeL2Error(succ_err);

      mfem::out << "||ψ-ψk||: " << norm_reduced_gradient << std::endl;
      mfem::out << "Volume Fraction: " << currVol / vol << std::endl;

      if (norm_reduced_gradient < tol)
      {
         break;
      }
   }

   return 0;
}