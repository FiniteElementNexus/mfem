//                                MFEM Example 9
//
// Compile with: make linadv
//
// Sample runs:
//    ./linadv -m ../data/periodic-segment.mesh -p 0 -r 2 -dt 0.005
//    ./linadv -m ../data/periodic-square.mesh -p 0 -r 2 -dt 0.01 -tf 10
//    ./linadv -m ../data/periodic-hexagon.mesh -p 0 -r 2 -dt 0.01 -tf 10
//    ./linadv -m ../data/periodic-square.mesh -p 1 -r 2 -dt 0.005 -tf 9
//    ./linadv -m ../data/periodic-hexagon.mesh -p 1 -r 2 -dt 0.005 -tf 9
//    ./linadv -m ../data/amr-quad.mesh -p 1 -r 2 -dt 0.002 -tf 9
//    ./linadv -m ../data/amr-quad.mesh -p 1 -r 2 -dt 0.02 -s 13 -tf 9
//    ./linadv -m ../data/star-q3.mesh -p 1 -r 2 -dt 0.005 -tf 9
//    ./linadv -m ../data/star-mixed.mesh -p 1 -r 2 -dt 0.005 -tf 9
//    ./linadv -m ../data/disc-nurbs.mesh -p 1 -r 3 -dt 0.005 -tf 9
//    ./linadv -m ../data/disc-nurbs.mesh -p 2 -r 3 -dt 0.005 -tf 9
//    ./linadv -m ../data/periodic-square.mesh -p 3 -r 4 -dt 0.0025 -tf 9 -vs 20
//    ./linadv -m ../data/periodic-cube.mesh -p 0 -r 2 -o 2 -dt 0.02 -tf 8
//    ./linadv -m ../data/periodic-square.msh -p 0 -r 2 -dt 0.005 -tf 2
//    ./linadv -m ../data/periodic-cube.msh -p 0 -r 1 -o 2 -tf 2
//
// Device sample runs:
//    ./linadv -pa
//    ./linadv -ea
//    ./linadv -fa
//    ./linadv -pa -m ../data/periodic-cube.mesh
//    ./linadv -pa -m ../data/periodic-cube.mesh -d cuda
//    ./linadv -ea -m ../data/periodic-cube.mesh -d cuda
//    ./linadv -fa -m ../data/periodic-cube.mesh -d cuda
//    ./linadv -pa -m ../data/amr-quad.mesh -p 1 -r 2 -dt 0.002 -tf 9 -d cuda
//
// Description:  This example code solves the time-dependent advection equation
//               du/dt + v.grad(u) = 0, where v is a given fluid velocity, and
//               u0(x)=u(0,x) is a given initial condition.
//
//               The example demonstrates the use of Discontinuous Galerkin (DG)
//               bilinear forms in MFEM (face integrators), the use of implicit
//               and explicit ODE time integrators, the definition of periodic
//               boundary conditions through periodic meshes, as well as the use
//               of GLVis for persistent visualization of a time-evolving
//               solution. The saving of time-dependent data files for external
//               visualization with VisIt (visit.llnl.gov) and ParaView
//               (paraview.org) is also illustrated.

#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

using namespace std;
using namespace mfem;

// Choice for the problem setup. The fluid velocity, initial condition and
// inflow boundary condition are chosen based on this parameter.
int problem;

// Velocity coefficient
void velocity_function(const Vector &x, Vector &v);

// Initial condition
void u0_function(const Vector &x, Vector &u);

// Inflow boundary condition
//real_t inflow_function(const Vector &x);

// Mesh bounding box
Vector bb_min, bb_max;

/*
class DG_Solver : public Solver
{
private:
   SparseMatrix &M, &K, A;
   GMRESSolver linear_solver;
   BlockILU prec;
   real_t dt;
public:
   DG_Solver(SparseMatrix &M_, SparseMatrix &K_, const FiniteElementSpace &fes)
      : M(M_),
        K(K_),
        prec(fes.GetFE(0)->GetDof(),
             BlockILU::Reordering::MINIMUM_DISCARDED_FILL),
        dt(-1.0)
   {
      linear_solver.iterative_mode = false;
      linear_solver.SetRelTol(1e-9);
      linear_solver.SetAbsTol(0.0);
      linear_solver.SetMaxIter(100);
      linear_solver.SetPrintLevel(0);
      linear_solver.SetPreconditioner(prec);
   }

   void SetTimeStep(real_t dt_)
   {
      if (dt_ != dt)
      {
         dt = dt_;
         // Form operator A = M - dt*K
         A = K;
         A *= -dt;
         A += M;

         // this will also call SetOperator on the preconditioner
         linear_solver.SetOperator(A);
      }
   }

   void SetOperator(const Operator &op)
   {
      linear_solver.SetOperator(op);
   }

   virtual void Mult(const Vector &x, Vector &y) const
   {
      linear_solver.Mult(x, y);
   }
};
//*/

/** A time-dependent operator for the right-hand side of the ODE. The DG weak
    form of du/dt = -v.grad(u) is M du/dt = K u + b, where M and K are the mass
    and advection matrices, and b describes the flow on the boundary. This can
    be written as a general ODE, du/dt = M^{-1} (K u + b), and this class is
    used to evaluate the right-hand side. */
class FE_Evolution : public TimeDependentOperator
{
protected:
   SparseMatrix M, K;
   const Vector &lumpedmassmatrix;
   const int nDofs;
   const int numVar;

   mutable Vector z;

public:
   FE_Evolution(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_);

   virtual void Mult(const Vector &x, Vector &y) const = 0;
   virtual double CalcGraphViscosity(const int i, const int j) const;
   //virtual void ImplicitSolve(const real_t dt, const Vector &x, Vector &k);

   virtual ~FE_Evolution();
};

class LowOrder : public FE_Evolution
{
   public:
   LowOrder(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_);
   virtual void Mult(const Vector &x, Vector &y) const;

   virtual ~LowOrder();
};

class HighOrderTargetScheme : public FE_Evolution
{
   public:
   HighOrderTargetScheme(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_);
   virtual void Mult(const Vector &x, Vector &y) const;

   virtual ~HighOrderTargetScheme();
};

class MCL : public FE_Evolution
{
   public:
   MCL(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_);
   virtual void Mult(const Vector &x, Vector &y) const;

   virtual ~MCL();
};

// Implementation of class FE_Evolution
FE_Evolution::FE_Evolution(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_)
   : TimeDependentOperator(M_.FESpace()->GetTrueVSize() * numVar_),
     M(M_.SpMat()), K(K_.SpMat()), lumpedmassmatrix(lumpedmassmatrix_), z(nDofs), nDofs(lumpedmassmatrix_.Size()), numVar(numVar_)
{ }

double FE_Evolution::CalcGraphViscosity(const int i, const int j) const
{ 
   return max(abs(K(i,j)), abs(K(j,i)));
}

FE_Evolution::~FE_Evolution()
{ }

LowOrder::LowOrder(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_) 
   : FE_Evolution(M_, K_, lumpedmassmatrix_, numVar_)
{ }

void LowOrder::Mult(const Vector &x, Vector &y) const
{  
   MFEM_VERIFY(numVar * nDofs == x.Size(), "Vector size wrong!");
   MFEM_VERIFY(y.Size() == numVar * nDofs, "Vector size wrong!");
   const auto II = K.ReadI();
   const auto JJ = K.ReadJ();
   const auto KK = K.ReadData();
   double dij;

   for(int n = 0; n < numVar; n++)
   {
      for(int i = 0; i < nDofs; i++)
      {
         const int begin = II[i];
         const int end = II[i+1];

         y(i + n * nDofs) = 0.0;
         for(int j = begin; j < end; j++)
         {
            if( i == JJ[j] ) {continue;}

            dij = CalcGraphViscosity(i,JJ[j]);
            y(i + n * nDofs) += (dij - KK[j]) * (x(JJ[j] + n * nDofs) - x(i + n * nDofs));
         }
         y(i + n * nDofs) /= lumpedmassmatrix(i); 
      }
   }
}

LowOrder::~LowOrder()
{ }

HighOrderTargetScheme::HighOrderTargetScheme(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_) 
   : FE_Evolution(M_, K_, lumpedmassmatrix_, numVar_)
{ }

void HighOrderTargetScheme::Mult(const Vector &x, Vector &y) const
{  
   const auto II = K.ReadI();
   const auto JJ = K.ReadJ();
   const auto KK = K.ReadData();
   const auto MM = M.ReadData();
   double dij;

   // compute low order time derivatives
   for(int n = 0; n < numVar; n++)
   {
      for(int i = 0; i < nDofs; i++)
      {
         const int begin = II[i];
         const int end = II[i+1];

         z(i) = 0.0;
         for(int j = begin; j < end; j++)
         {
            if( i == JJ[j] ) {continue;}

            dij = CalcGraphViscosity(i,JJ[j]);
            z(i) += (dij - KK[j]) * (x(JJ[j] + n * nDofs) - x(i + n * nDofs));
         }
         z(i) /= lumpedmassmatrix(i); 
      }

      for(int i = 0; i < nDofs; i++)
      {
         const int begin = II[i];
         const int end = II[i+1];

         y(i + n * nDofs) = 0.0;
         for(int j = begin; j < end; j++)
         {
            if( i == JJ[j] ) {continue;}

            //dij = CalcGraphViscosity(i,JJ[j]);
            y(i + n * nDofs) += - KK[j] * (x(JJ[j] + n * nDofs) - x(i + n * nDofs)) + MM[j] * (z(i) - z(JJ[j]));
         }
         y(i + n * nDofs) /= lumpedmassmatrix(i); 
      } 
   }
}

HighOrderTargetScheme::~HighOrderTargetScheme()
{ }

MCL::MCL(BilinearForm &M_, BilinearForm &K_, Vector &lumpedmassmatrix_, int numVar_) 
   : FE_Evolution(M_, K_, lumpedmassmatrix_, numVar_)
{ }

void MCL::Mult(const Vector &x, Vector &y) const
{  
   const auto II = K.ReadI();
   const auto JJ = K.ReadJ();
   const auto KK = K.ReadData();
   const auto MM = M.ReadData();
   double dij, fij, fij_star, fij_bound, wij, wji;

   for(int n = 0; n < numVar; n++)
   {
      // compute low order time derivatives
      for(int i = 0; i < nDofs; i++)
      {
         const int begin = II[i];
         const int end = II[i+1];

         z(i) = 0.0;
         for(int j = begin; j < end; j++)
         {
            if( i == JJ[j] ) {continue;}

            dij = CalcGraphViscosity(i,JJ[j]);
            z(i) += (dij - KK[j]) * (x(JJ[j] + n * nDofs) - x(i + n * nDofs));
         }
         z(i) /= lumpedmassmatrix(i); 

      }

      for(int i = 0; i < nDofs; i++)
      {
         const int begin = II[i];
         const int end = II[i+1];
      
         // find the local bounds for the i-th node
         double ui_min = x(i + n * nDofs);
         double ui_max = ui_min;
         for(int j = begin; j < end; j++)
         {
            ui_max = max(ui_max, x(JJ[j] + n * nDofs));
            ui_min = min(ui_min, x(JJ[j] + n * nDofs));
         }
      
         y(i + n * nDofs) = 0.0;
         for(int j = begin; j < end; j++)
         {
            if( i == JJ[j] ) {continue;}
         
            // find the local bounds for the JJ[j]-th node
            double uj_min = x(JJ[j] + n * nDofs);
            double uj_max = uj_min;
            const int begin_j = II[JJ[j]];
            const int end_j = II[JJ[j]+1];
            for(int k = begin_j; k < end_j; k++)
            {  
               uj_max = max(uj_max, x(JJ[k] + n * nDofs));
               uj_min = min(uj_min, x(JJ[k] + n * nDofs));
            }
          
            dij = CalcGraphViscosity(i,JJ[j]);

            // compute target flux
            fij = MM[j] * (z(i) - z(JJ[j])) + dij * (x(i + n * nDofs) - x(JJ[j] + n * nDofs));

            //limit target flux to enforce local bounds for the bar states (note, that dij = dji)
            wij = dij * (x(i + n * nDofs) + x(JJ[j] + n * nDofs))  - KK[j] * (x(JJ[j] + n * nDofs) - x(i + n * nDofs));
            wji = dij * (x(i + n * nDofs) + x(JJ[j] + n * nDofs))  - K(JJ[j], i) * (x(i + n * nDofs) - x(JJ[j] + n * nDofs)); 
            if(fij > 0)
            {
               fij_bound = min(2.0 * dij * ui_max - wij, wji - 2.0 * dij * uj_min);
               fij_star = min(fij, fij_bound);

               // to get rid of rounding errors wich influence the sign 
               //fij_star = max(0.0, fij_star);
            }
            else
            {
               fij_bound = max(2.0 * dij * ui_min - wij, wji - 2.0 * dij * uj_max);
               fij_star = max(fij, fij_bound);

               // to get rid of rounding errors wich influence the sign
               //fij_star = min(0.0, fij_star);
            }
            y(i + n * nDofs) += (dij - KK[j]) * (x(JJ[j] + n * nDofs) - x(i + n * nDofs) ) + fij_star ;
         }  
         y(i + n * nDofs) /= lumpedmassmatrix(i);  
      }
   }
}

MCL::~MCL()
{ }




int main(int argc, char *argv[])
{
   // 1. Parse command-line options.
   problem = 0;
   const char *mesh_file = "../data/periodic-hexagon.mesh";
   int ref_levels = 2;
   int order = 1;
   int scheme = 1;
   bool pa = false;
   bool ea = false;
   bool fa = false;
   const char *device_config = "cpu";
   int ode_solver_type = 2;
   real_t t_final = 1.0;
   real_t dt = 0.001;
   bool visualization = true;
   bool visit = false;
   bool paraview = false;
   bool binary = false;
   int vis_steps = 10;

   int precision = 8;
   cout.precision(precision);

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&problem, "-p", "--problem",
                  "Problem setup to use. See options in velocity_function().");
   args.AddOption(&scheme, "-sc", "--scheme",
                  "Finite element scheme to use. 1 for low order scheme, 2 for high order target scheme, 3 for monolithic convex limiting!");
   args.AddOption(&ref_levels, "-r", "--refine",
                  "Number of times to refine the mesh uniformly.");
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&pa, "-pa", "--partial-assembly", "-no-pa",
                  "--no-partial-assembly", "Enable Partial Assembly.");
   args.AddOption(&ea, "-ea", "--element-assembly", "-no-ea",
                  "--no-element-assembly", "Enable Element Assembly.");
   args.AddOption(&fa, "-fa", "--full-assembly", "-no-fa",
                  "--no-full-assembly", "Enable Full Assembly.");
   args.AddOption(&device_config, "-d", "--device",
                  "Device configuration string, see Device::Configure().");
   args.AddOption(&ode_solver_type, "-s", "--ode-solver",
                  "ODE solver: 1 - Forward Euler,\n\t"
                  "            2 - RK2 SSP, 3 - RK3 SSP, 4 - RK4, 6 - RK6,\n\t"
                  "            11 - Backward Euler,\n\t"
                  "            12 - SDIRK23 (L-stable), 13 - SDIRK33,\n\t"
                  "            22 - Implicit Midpoint Method,\n\t"
                  "            23 - SDIRK23 (A-stable), 24 - SDIRK34");
   args.AddOption(&t_final, "-tf", "--t-final",
                  "Final time; start time is 0.");
   args.AddOption(&dt, "-dt", "--time-step",
                  "Time step.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&visit, "-visit", "--visit-datafiles", "-no-visit",
                  "--no-visit-datafiles",
                  "Save data files for VisIt (visit.llnl.gov) visualization.");
   args.AddOption(&paraview, "-paraview", "--paraview-datafiles", "-no-paraview",
                  "--no-paraview-datafiles",
                  "Save data files for ParaView (paraview.org) visualization.");
   args.AddOption(&binary, "-binary", "--binary-datafiles", "-ascii",
                  "--ascii-datafiles",
                  "Use binary (Sidre) or ascii format for VisIt data files.");
   args.AddOption(&vis_steps, "-vs", "--visualization-steps",
                  "Visualize every n-th timestep.");
   args.Parse();
   if (!args.Good())
   {
      args.PrintUsage(cout);
      return 1;
   }
   args.PrintOptions(cout);

   Device device(device_config);
   device.Print();

   // 2. Read the mesh from the given mesh file. We can handle geometrically
   //    periodic meshes in this code.
   Mesh mesh(mesh_file, 1, 1);
   int dim = mesh.Dimension();

   // 3. Define the ODE solver used for time integration. Several explicit
   //    Runge-Kutta methods are available.
   ODESolver *ode_solver = NULL;
   switch (ode_solver_type)
   {
      // Explicit methods
      case 1: ode_solver = new ForwardEulerSolver; break;
      case 2: ode_solver = new RK2Solver(1.0); break;
      case 3: ode_solver = new RK3SSPSolver; break;
      case 4: ode_solver = new RK4Solver; break;
      case 6: ode_solver = new RK6Solver; break;

      default:
         cout << "Unknown ODE solver type: " << ode_solver_type << '\n';
         return 3;
   }

   // 4. Refine the mesh to increase the resolution. In this example we do
   //    'ref_levels' of uniform refinement, where 'ref_levels' is a
   //    command-line parameter. If the mesh is of NURBS type, we convert it to
   //    a (piecewise-polynomial) high-order mesh.
   for (int lev = 0; lev < ref_levels; lev++)
   {
      mesh.UniformRefinement();
   }
   if (mesh.NURBSext)
   {
      mesh.SetCurvature(max(order, 1));
   }
   mesh.GetBoundingBox(bb_min, bb_max, max(order, 1));

   // 5. Define the discontinuous DG finite element space of the given
   //    polynomial order on the refined mesh.
   int numVar;
   switch(problem)
   {
      case 0:
      case 1: numVar = 1; break; // scalar
      case 2: numVar = 2; break;  
   }
   H1_FECollection fec(order, dim, BasisType::Positive);
   FiniteElementSpace fes(&mesh, &fec);
   FiniteElementSpace vfes(&mesh, &fec, numVar);

   cout << "Number of unknowns: " << fes.GetVSize() << endl;

   // 6. Set up and assemble the bilinear and linear forms corresponding to the
   //    DG discretization. The DGTraceIntegrator involves integrals over mesh
   //    interior faces.
   VectorFunctionCoefficient velocity(dim, velocity_function);
   //FunctionCoefficient inflow(inflow_function);
   VectorFunctionCoefficient u0(numVar, u0_function);

   BilinearForm m(&fes);
   BilinearForm k(&fes);
   BilinearForm lumped_m(&fes);

   if (pa)
   {
      m.SetAssemblyLevel(AssemblyLevel::PARTIAL);
      k.SetAssemblyLevel(AssemblyLevel::PARTIAL);
   }
   else if (ea)
   {
      m.SetAssemblyLevel(AssemblyLevel::ELEMENT);
      k.SetAssemblyLevel(AssemblyLevel::ELEMENT);
   }
   else if (fa)
   {
      m.SetAssemblyLevel(AssemblyLevel::FULL);
      k.SetAssemblyLevel(AssemblyLevel::FULL);
   }
   m.AddDomainIntegrator(new MassIntegrator);
   lumped_m.AddDomainIntegrator(new LumpedIntegrator(new MassIntegrator()));
   //constexpr real_t alpha = - 1.0;
   //k.AddDomainIntegrator(new TransposeIntegrator(new ConvectionIntegrator(velocity, alpha)));
   k.AddDomainIntegrator(new ConvectionIntegrator(velocity));
   //k.AddInteriorFaceIntegrator(new NonconservativeDGTraceIntegrator(velocity, alpha));
   //k.AddBdrFaceIntegrator(
   //   new NonconservativeDGTraceIntegrator(velocity, alpha));

   //LinearForm b(&fes);
   //b.AddBdrFaceIntegrator(
   //   new BoundaryFlowIntegrator(inflow, velocity, alpha));

   m.Assemble();
   lumped_m.Assemble();
   int skip_zeros = 0;
   k.Assemble(skip_zeros);
   //b.Assemble();
   m.Finalize();
   lumped_m.Finalize();
   k.Finalize(skip_zeros);

   Vector lumpedmassmatrix(fes.GetVSize());
   lumped_m.SpMat().GetDiag(lumpedmassmatrix);

   // 7. Define the initial conditions, save the corresponding grid function to
   //    a file and (optionally) save data in the VisIt format and initialize
   //    GLVis visualization.
   GridFunction u(&vfes);
   u.ProjectCoefficient(u0);

   {
      ofstream omesh("output/linadv.mesh");
      omesh.precision(precision);
      mesh.Print(omesh);
      ofstream osol("output/linadv-init.gf");
      osol.precision(precision);
      u.Save(osol);
   }

   // Create data collection for solution output: either VisItDataCollection for
   // ascii data files, or SidreDataCollection for binary data files.
   DataCollection *dc = NULL;
   if (visit)
   {
      if (binary)
      {
#ifdef MFEM_USE_SIDRE
         dc = new SidreDataCollection("Example9", &mesh);
#else
         MFEM_ABORT("Must build with MFEM_USE_SIDRE=YES for binary output.");
#endif
      }
      else
      {
         dc = new VisItDataCollection("Example9", &mesh);
         dc->SetPrecision(precision);
      }
      dc->RegisterField("solution", &u);
      dc->SetCycle(0);
      dc->SetTime(0.0);
      dc->Save();
   }

   ParaViewDataCollection *pd = NULL;
   if (paraview)
   {
      pd = new ParaViewDataCollection("Example9", &mesh);
      pd->SetPrefixPath("ParaView");
      pd->RegisterField("solution", &u);
      pd->SetLevelsOfDetail(order);
      pd->SetDataFormat(VTKFormat::BINARY);
      pd->SetHighOrderOutput(true);
      pd->SetCycle(0);
      pd->SetTime(0.0);
      pd->Save();
   }

   socketstream sout;
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      sout.open(vishost, visport);
      if (!sout)
      {
         cout << "Unable to connect to GLVis server at "
              << vishost << ':' << visport << endl;
         visualization = false;
         cout << "GLVis visualization disabled.\n";
      }
      else
      {
         sout.precision(precision);
         sout << "solution\n" << mesh << u;
         sout << "keys mcljUUUUU\n";
         if(dim == 1)
         {
            sout << "keys RR\n"; 
         }
         else if(dim == 2)
         {
            sout << "keys Rm\n"; 
         }
         sout << "window_geometry "
            << 0 << " " << 0 << " " << 1080 << " " << 1080 << "\n";
         sout << flush;
         cout << "GLVis visualization paused."
              << " Press space (in the GLVis window) to resume it.\n";
      }
   }

   // 8. Define the time-dependent evolution operator describing the ODE
   //    right-hand side, and perform time-integration (looping over the time
   //    iterations, ti, with a time-step dt).

   FE_Evolution *adv = NULL;
   switch(scheme)
   {
      case 1: adv = new LowOrder(m, k, lumpedmassmatrix, numVar); break;
      case 2: adv = new HighOrderTargetScheme(m, k, lumpedmassmatrix, numVar); break;
      case 3: adv = new MCL(m, k, lumpedmassmatrix, numVar); break;
      default: MFEM_ABORT("Unknown scheme!");
   }

   real_t t = 0.0;
   adv->SetTime(t);
   ode_solver->Init(*adv);

   bool done = false;
   for (int ti = 0; !done; )
   {
      real_t dt_real = min(dt, t_final - t);
      ode_solver->Step(u, t, dt_real);
      ti++;

      done = (t >= t_final - 1e-8*dt);

      if (done || ti % vis_steps == 0)
      {
         cout << "time step: " << ti << ", time: " << t << endl;

         if (visualization)
         {
            sout << "solution\n" << mesh << u << flush;
         }

         if (visit)
         {
            dc->SetCycle(ti);
            dc->SetTime(t);
            dc->Save();
         }

         if (paraview)
         {
            pd->SetCycle(ti);
            pd->SetTime(t);
            pd->Save();
         }
      }
   }

   // 9. Save the final solution. This output can be viewed later using GLVis:
   //    "glvis -m linadv.mesh -g linadv-final.gf".
   {
      ofstream osol("output/linadv-final.gf");
      osol.precision(precision);
      u.Save(osol);
   }

   // 10. Free the used memory.
   delete ode_solver;
   delete pd;
   delete dc;
   delete adv;

   return 0;
}

// Velocity coefficient
void velocity_function(const Vector &x, Vector &v)
{
   int dim = x.Size();

   // map to the reference [-1,1] domain
   Vector X(dim);
   for (int i = 0; i < dim; i++)
   {
      real_t center = (bb_min[i] + bb_max[i]) * 0.5;
      X(i) = 2 * (x(i) - center) / (bb_max[i] - bb_min[i]);
   }

   switch (problem)
   {
      case 0:
      {
         // Translations in 1D, 2D, and 3D
         switch (dim)
         {
            case 1: v(0) = 1.0; break;
            case 2: v(0) = sqrt(2./3.); v(1) = sqrt(1./3.); break;
            case 3: v(0) = sqrt(3./6.); v(1) = sqrt(2./6.); v(2) = sqrt(1./6.);
               break;
         }
         break;
      }
      case 1:
      {
         v(0) = 2.0 * M_PI * (- x(1));
         v(1) = 2.0 * M_PI * (x(0) );
         break;
      }

      case 2:
      {
         // Clockwise rotation in 2D around the origin
         const real_t w = M_PI/2;
         switch (dim)
         {
            case 1: v(0) = 1.0; break;
            case 2: v(0) = w*X(1); v(1) = -w*X(0); break;
            case 3: v(0) = w*X(1); v(1) = -w*X(0); v(2) = 0.0; break;
         }
         break;
      }
      case 3:
      {
         // Clockwise twisting rotation in 2D around the origin
         const real_t w = M_PI/2;
         real_t d = max((X(0)+1.)*(1.-X(0)),0.) * max((X(1)+1.)*(1.-X(1)),0.);
         d = d*d;
         switch (dim)
         {
            case 1: v(0) = 1.0; break;
            case 2: v(0) = d*w*X(1); v(1) = -d*w*X(0); break;
            case 3: v(0) = d*w*X(1); v(1) = -d*w*X(0); v(2) = 0.0; break;
         }
         break;
      }
   }
}

// Initial condition
void u0_function(const Vector &x, Vector &u)
{
   int dim = x.Size();
   int numVar = u.Size();

   // map to the reference [-1,1] domain
   Vector X(dim);
   for (int i = 0; i < dim; i++)
   {
      real_t center = (bb_min[i] + bb_max[i]) * 0.5;
      X(i) = 2 * (x(i) - center) / (bb_max[i] - bb_min[i]);
   }

   switch (problem)
   {
      case 0:
      {
         //if(dim != 1)
         //{
         //   MFEM_ABORT("Problem 0 only works in 1D!");
         //}
         //if(numVar != 1)
         //{
         //   MFEM_ABORT("Problem 0 only works for scalar!");
         //}
         
         if(x(0) < 0.9 && x(0) > 0.5)
         {
            u(0) = exp(10) * exp(1.0 / (0.5 - x(0))) * exp(1.0 / (x(0) - 0.9));
         }
         else if( x(0) < 0.4 && x(0) > 0.2)
         {
            u(0) = 1.0;
         }
         else 
         {
            u(0) = 0.0;
         }
         break;
      }
      case 1:
      {
         if (dim != 2) 
         { 
            MFEM_ABORT("Solid body rotation does not work in 1D."); 
         }
         //if(numVar != 1)
         //{
         //   MFEM_ABORT("Problem 1 only works for scalar!");
         //}
         else 
         {
            // Initial condition defined on [0,1]^2
            Vector y = x;
            y *= 0.5;
            y += 0.5;
            double s = 0.15;
            double cone = sqrt(pow(y(0) - 0.5, 2.0) + pow(y(1) - 0.25, 2.0));
            double hump = sqrt(pow(y(0) - 0.25, 2.0) + pow(y(1) - 0.5, 2.0));
            u(0) = (1.0 - cone / s) * (cone <= s) + 0.25 * (1.0 + cos(M_PI*hump / s)) * (hump <= s) +
                ((sqrt(pow(y(0) - 0.5, 2.0) + pow(y(1) - 0.75, 2.0)) <= s ) && ( abs(y(0) -0.5) >= 0.025 || (y(1) >= 0.85) ) ? 1.0 : 0.0);
         }
         break;
      }




      case 2:
      {
         switch (dim)
         {
            case 1:
               u(0) = exp(-40.*pow(X(0)-0.5,2));
            case 2:
            case 3:
            {
               real_t rx = 0.45, ry = 0.25, cx = 0., cy = -0.2, w = 10.;
               if (dim == 3)
               {
                  const real_t s = (1. + 0.25*cos(2*M_PI*X(2)));
                  rx *= s;
                  ry *= s;
               }
               u(0) = ( std::erfc(w*(X(0)-cx-rx))*std::erfc(-w*(X(0)-cx+rx)) *
                        std::erfc(w*(X(1)-cy-ry))*std::erfc(-w*(X(1)-cy+ry)) )/16;
            }
         }
         u(1) = u(0);
         break;
      }
      case 3:
      {
         real_t x_ = X(0), y_ = X(1), rho, phi;
         rho = std::hypot(x_, y_);
         phi = atan2(y_, x_);
         u(0) = pow(sin(M_PI*rho),2)*sin(3*phi);
         break;
      }
      case 4:
      {
         const real_t f = M_PI;
         u(0) = sin(f*X(0))*sin(f*X(1));
         break;
      }
   }
   for(int n = 1; n < numVar; n++)
   {
      u(n) = pow(-1.0, double(n)) * u(0);
   }
}

// Inflow boundary condition (zero for the problems considered in this example)
real_t inflow_function(const Vector &x)
{
   switch (problem)
   {
      case 0:
      case 1:
      case 2:
      case 3: return 0.0;
   }
   return 0.0;
}
