// Copyright (c) 2010-2024, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "stab_navsto.hpp"

using namespace mfem;

StabInNavStoIntegrator::StabInNavStoIntegrator(Coefficient &mu_,
                                               Tau &t, Tau &d, StabType s)
 : c_mu(&mu_), tau(&t), delta(&d), stab(s)
{

}

void StabInNavStoIntegrator::SetDim(int dim_)
{
  if (dim_ != dim)
  {
     dim = dim_;
     u.SetSize(dim);
     res.SetSize(dim);
     up.SetSize(dim);
     grad_u.SetSize(dim);
     hess_u.SetSize(dim, (dim*(dim+1))/2);
     grad_p.SetSize(dim);
     hmap.SetSize(dim,dim);

     if (dim == 2)
     {
        hmap(0,0) = 0;
        hmap(0,1) =  hmap(1,0) =  1;
        hmap(1,1) = 2; 
     }
     else if (dim == 2)
     {
        hmap(0,0) = 0;
        hmap(0,1) = hmap(1,0) = 1;
        hmap(0,2) = hmap(2,0) = 2;
        hmap(1,1) = 3;
        hmap(1,2) = hmap(2,1) = 4;
        hmap(2,2) = 5;
     }
     else
     {
        mfem_error("Only implemented for 2D and 3D");
     }
  }
}

real_t StabInNavStoIntegrator::GetElementEnergy(
   const Array<const FiniteElement *>&el,
   ElementTransformation &Tr,
   const Array<const Vector *>&elfun)
{
   if (el.Size() != 2)
   {
      mfem_error("StabInNavStoIntegrator::GetElementEnergy"
                 " has incorrect block finite element space size!");
   }
   SetDim(el[0]->GetDim());
   int dof_u = el[0]->GetDof();


   sh_u.SetSize(dof_u);
   elf_u.UseExternalData(elfun[0]->GetData(), dof_u, dim);

   int intorder = 2*el[0]->GetOrder() + 3; // <---
   const IntegrationRule &ir = IntRules.Get(el[0]->GetGeomType(), intorder);

   real_t energy = 0.0;

   for (int i = 0; i < ir.GetNPoints(); ++i)
   {
      const IntegrationPoint &ip = ir.IntPoint(i);
      Tr.SetIntPoint(&ip);

      real_t w = ip.weight * Tr.Weight();

      el[0]->CalcPhysShape(Tr, sh_u);
      elf_u.MultTranspose(sh_u, u);

      energy += w*(u*u)/2;
   }

   return energy;
}

void StabInNavStoIntegrator::AssembleElementVector(
   const Array<const FiniteElement *> &el,
   ElementTransformation &Tr,
   const Array<const Vector *> &elfun,
   const Array<Vector *> &elvec)
{
   if (el.Size() != 2)
   {
      mfem_error("StabInNavStoIntegrator::AssembleElementVector"
                 " has finite element space of incorrect block number");
   }

   int dof_u = el[0]->GetDof();
   int dof_p = el[1]->GetDof();

   SetDim(el[0]->GetDim());
   int spaceDim = Tr.GetSpaceDim();

   if (dim != spaceDim)
   {
      mfem_error("StabInNavStoIntegrator::AssembleElementVector"
                 " is not defined on manifold meshes");
   }
   elvec[0]->SetSize(dof_u*dim);
   elvec[1]->SetSize(dof_p);

   *elvec[0] = 0.0;
   *elvec[1] = 0.0;

   elf_u.UseExternalData(elfun[0]->GetData(), dof_u, dim);
   elv_u.UseExternalData(elvec[0]->GetData(), dof_u, dim);

   sh_u.SetSize(dof_u);
   shg_u.SetSize(dof_u, dim);
   ushg_u.SetSize(dof_u);
   shh_u.SetSize(dof_u, (dim*(dim+1))/2);
   sh_p.SetSize(dof_p);
   shg_p.SetSize(dof_p, dim);

   int intorder = 2*el[0]->GetOrder() + 3; // <---
   const IntegrationRule &ir = IntRules.Get(el[0]->GetGeomType(), intorder);

   for (int i = 0; i < ir.GetNPoints(); ++i)
   {
      const IntegrationPoint &ip = ir.IntPoint(i);
      Tr.SetIntPoint(&ip);
      real_t w = ip.weight * Tr.Weight();
      real_t mu = c_mu->Eval(Tr, ip);

      // Compute shape and interpolate
      el[0]->CalcPhysShape(Tr, sh_u);
      elf_u.MultTranspose(sh_u, u);

      el[0]->CalcPhysDShape(Tr, shg_u);
      shg_u.Mult(u, ushg_u);
      MultAtB(elf_u, shg_u, grad_u);

      el[0]->CalcPhysHessian(Tr,shh_u);
      MultAtB(elf_u, shh_u, hess_u);

      el[1]->CalcPhysShape(Tr, sh_p);
      real_t p = sh_p*(*elfun[1]);

      el[1]->CalcPhysDShape(Tr, shg_p);
      shg_p.MultTranspose(*elfun[1], grad_p);

      // Compute strong residual
      grad_u.Mult(u,res);   // Add convection
      res += grad_p;        // Add pressure
      for (int i = 0; i < dim; ++i)
      {
         for (int j = 0; j < dim; ++j)
         {
            res[j] -= mu*(hess_u(j,hmap(i,i)) + 
                          hess_u(i,hmap(j,i))); // Add diffusion
         }
      }

      // Compute stability params
      real_t t = tau->Eval(Tr, ip);
      real_t d = delta->Eval(Tr, ip);

      // Compute momentum weak residual
      flux.Diag(-p + d*grad_u.Trace(),dim);  // Add pressure & LSIC to flux
      grad_u.Symmetrize();                   // Grad to strain
      flux.Add(2*mu,grad_u);                 // Add stress to flux
      AddMult_a_VVt(-1.0, u, flux);          // Add convection to flux
      AddMult_a_VWt(t, res, u, flux);        // Add SUPG to flux --> check order u and res
      AddMult_a_ABt(w, shg_u, flux, elv_u);  // Add flux term to rhs

      // Compute momentum weak residual
      elvec[1]->Add(w*grad_u.Trace(), sh_p); // Add Galerkin term
      shg_p.Mult(res, sh_p);                 // PSPG help term
      elvec[1]->Add(w*t, sh_p);             // Add PSPG term  - sign looks worng?
   }
}

void StabInNavStoIntegrator::AssembleElementGrad(
   const Array<const FiniteElement*> &el,
   ElementTransformation &Tr,
   const Array<const Vector *> &elfun,
   const Array2D<DenseMatrix *> &elmats)
{
   int dof_u = el[0]->GetDof();
   int dof_p = el[1]->GetDof();

   SetDim(el[0]->GetDim());

   elf_u.UseExternalData(elfun[0]->GetData(), dof_u, dim);

   elmats(0,0)->SetSize(dof_u*dim, dof_u*dim);
   elmats(0,1)->SetSize(dof_u*dim, dof_p);
   elmats(1,0)->SetSize(dof_p, dof_u*dim);
   elmats(1,1)->SetSize(dof_p, dof_p);

   *elmats(0,0) = 0.0;
   *elmats(0,1) = 0.0;
   *elmats(1,0) = 0.0;
   *elmats(1,1) = 0.0;

   sh_u.SetSize(dof_u);
   shg_u.SetSize(dof_u, dim);
   ushg_u.SetSize(dof_u);
   sh_p.SetSize(dof_p);
   shg_p.SetSize(dof_p, dim);

   int intorder = 2*el[0]->GetOrder() + 3; // <---
   const IntegrationRule &ir = IntRules.Get(el[0]->GetGeomType(), intorder);

   for (int i = 0; i < ir.GetNPoints(); ++i)
   {
      const IntegrationPoint &ip = ir.IntPoint(i);
      Tr.SetIntPoint(&ip);
      real_t w = ip.weight * Tr.Weight();
      real_t mu = c_mu->Eval(Tr, ip);
      real_t t = tau->Eval(Tr, ip);
      real_t d = delta->Eval(Tr, ip);

      el[0]->CalcPhysShape(Tr, sh_u);
      elf_u.MultTranspose(sh_u, u);

      el[0]->CalcPhysDShape(Tr, shg_u);
      MultAtB(elf_u, shg_u, grad_u);

      shg_u.Mult(u, ushg_u);

      el[1]->CalcPhysShape(Tr, sh_p);
      real_t p = sh_p*(*elfun[1]);

      el[1]->CalcPhysDShape(Tr, shg_p);
      shg_p.MultTranspose(*elfun[1], grad_p);

      // u,u block
      for (int i_u = 0; i_u < dof_u; ++i_u)
      {
         for (int j_u = 0; j_u < dof_u; ++j_u)
         {
            // Diffusion
            real_t mat = 0.0;
            for (int dim_u = 0; dim_u < dim; ++dim_u)
            {
              mat += shg_u(i_u,dim_u)*shg_u(j_u,dim_u);
            }
            mat *= mu;

            // Convection
            mat -= ushg_u(i_u)*sh_u(j_u);      // Galerkin
            mat += t*ushg_u(i_u)*ushg_u(j_u);  // SUPG

            mat *= w;
            for (int dim_u = 0; dim_u < dim; ++dim_u)
            {
               (*elmats(0,0))(i_u + dim_u*dof_u, j_u + dim_u*dof_u) += mat;
            }

            for (int i_dim = 0; i_dim < dim; ++i_dim)
            {
               for (int j_dim = 0; j_dim < dim; ++j_dim)
               {
                  (*elmats(0,0))(i_u + i_dim*dof_u, j_u + j_dim*dof_u) +=
                     (mu + d)*shg_u(i_u,j_dim)*shg_u(j_u,i_dim)*w;
               }
            }
         }
      }

      // u,p and p,u blocks
      for (int i_p = 0; i_p < dof_p; ++i_p)
      {
         for (int j_u = 0; j_u < dof_u; ++j_u)
         {
            for (int dim_u = 0; dim_u < dim; ++dim_u)
            {
               (*elmats(0,1))(j_u + dof_u * dim_u, i_p) += (shg_p(i_p, dim_u)*t*ushg_u(j_u)
                                                           -shg_u(j_u,dim_u)*sh_p(i_p))*w;
               (*elmats(1,0))(i_p, j_u + dof_u * dim_u) +=  shg_u(j_u,dim_u)*sh_p(i_p)*w;
            }
         }
      }

      // p,p block
      AddMult_a_AAt(w*t, shg_p, *elmats(1,1));
   }
}

void GeneralResidualMonitor::MonitorResidual(int it, real_t norm,
                                             const Vector &r, bool final)
{
   if (it == 0)
   {
      norm0 = norm;
   }

   if ((print_level > 0 &&  it%print_level == 0) || final)
   {
      mfem::out << prefix << " iteration " << std::setw(2) << it
                << " : ||r|| = " << norm
                << ",  ||r||/||r_0|| = " << 100*norm/norm0<<" % \n";
   }
}

void SystemResidualMonitor::MonitorResidual(int it, real_t norm,
                                            const Vector &r, bool final)
{
   bool print = (print_level > 0 &&  it%print_level == 0) || final;
   if (print || (it == 0))
   {
      Vector norm(nvar);

      for (int i = 0; i < nvar; ++i)
      {
         Vector r_i(r.GetData() + bOffsets[i], bOffsets[i+1] - bOffsets[i]);
         norm[i] = r_i.Norml2();
         if (it == 0) norm0[i] = norm[i];
      }

      if (print)
      {
         mfem::out << prefix << " iteration " << std::setw(3) << it <<"\n"
                   << " ||r||  \t"<< "||r||/||r_0||  \n";
         for (int i = 0; i < nvar; ++i)
         {
            mfem::out <<norm[i]<<"\t"<< 100*norm[i]/norm0[i]<<" % \n";
         }
      }
   }
}

JacobianPreconditioner::JacobianPreconditioner(Array<FiniteElementSpace *> &fes,
                                               SparseMatrix &mass,
                                               Array<int> &offsets)
   : Solver(offsets[2]), block_trueOffsets(offsets), pressure_mass(&mass)
{
   fes.Copy(spaces);

   gamma = 1.0001;

   // The mass matrix and preconditioner do not change every Newton cycle, so we
   // only need to define them once
   GSSmoother *mass_prec_gs = new GSSmoother(*pressure_mass);

   mass_prec = mass_prec_gs;

   CGSolver *mass_pcg_iter = new CGSolver();
   mass_pcg_iter->SetRelTol(1e-6);
   mass_pcg_iter->SetAbsTol(1e-12);
   mass_pcg_iter->SetMaxIter(10);
   mass_pcg_iter->SetPrintLevel(-1);
   mass_pcg_iter->SetPreconditioner(*mass_prec);
   //mass_pcg_iter->SetOperator(*pressure_mass);
  // mass_pcg_iter->SetOperator(jacobian->GetBlock(1,1));
   mass_pcg_iter->iterative_mode = false;

   mass_pcg = mass_pcg_iter;

   // The stiffness matrix does change every Newton cycle, so we will define it
   // during SetOperator
   stiff_pcg = NULL;
   stiff_prec = NULL;
}

void JacobianPreconditioner::Mult(const Vector &k, Vector &y) const
{
   int dof_u = block_trueOffsets[1]-block_trueOffsets[0];
   int dof_p = block_trueOffsets[2]-block_trueOffsets[1];

   // Extract the blocks from the input and output vectors
   Vector u_in(k.GetData() + block_trueOffsets[0],dof_u);
   Vector p_in(k.GetData() + block_trueOffsets[1],dof_p);

   Vector u_out(y.GetData() + block_trueOffsets[0],dof_u);
   Vector p_out(y.GetData() + block_trueOffsets[1],dof_p);

   Vector temp(dof_u);
   Vector temp2(dof_u);

   // Perform the block elimination for the preconditioner
   mass_pcg->Mult(p_in, p_out);
  // p_out *= -gamma;

   jacobian->GetBlock(0,1).Mult(p_out, temp);
   subtract(u_in, temp, temp2);

   stiff_pcg->Mult(temp2, u_out);
}

void JacobianPreconditioner::SetOperator(const Operator &op)
{
   jacobian = (BlockOperator *) &op;

   // Initialize the stiffness preconditioner and solver
   if (stiff_prec == NULL)
   {
      GSSmoother *stiff_prec_gs = new GSSmoother();

      stiff_prec = stiff_prec_gs;

      FGMRESSolver *stiff_pcg_iter = new FGMRESSolver();
      stiff_pcg_iter->SetRelTol(1e-6);
      stiff_pcg_iter->SetAbsTol(1e-12);
      stiff_pcg_iter->SetMaxIter(10);
      stiff_pcg_iter->SetPrintLevel(-1);
      stiff_pcg_iter->SetPreconditioner(*stiff_prec);
      stiff_pcg_iter->iterative_mode = false;

      stiff_pcg = stiff_pcg_iter;
   }

   // At each Newton cycle, compute the new stiffness preconditioner by updating
   // the iterative solver which, in turn, updates its preconditioner
   stiff_pcg->SetOperator(jacobian->GetBlock(0,0));
   mass_pcg->SetOperator(jacobian->GetBlock(1,1));
}

JacobianPreconditioner::~JacobianPreconditioner()
{
   delete mass_pcg;
   delete mass_prec;
   delete stiff_prec;
   delete stiff_pcg;
}


StabInNavStoOperator::StabInNavStoOperator(Array<FiniteElementSpace *> &fes,
                               Array<Array<int> *> &ess_bdr,
                               Array<int> &offsets,
                               real_t rel_tol,
                               real_t abs_tol,
                               int iter,
                               Coefficient &c_mu)
   : Operator(fes[0]->GetTrueVSize() + fes[1]->GetTrueVSize()),
     mu(c_mu), block_trueOffsets(offsets),
     newton_solver(),
     newton_monitor("Newton", 1, offsets),
     j_monitor("\t\t\t\tFGMRES", 25)
{
   Array<Vector *> rhs(2);
   rhs = NULL; // Set all entries in the array

   fes.Copy(spaces);

   // Define the block nonlinear form
   Hform = new BlockNonlinearForm(spaces);

   // Add the incompressible nav-sto integrator
   adv_gf = new GridFunction(fes[0], NULL); // Data will be loaded in Mult and GetGradient
   adv = new VectorGridFunctionCoefficient(adv_gf);

   tau = new FFH92Tau(adv, &mu, fes[0], 4.0);
   delta = new FF91Delta(adv, &mu, fes[0]);

   // Add the incompressible nav-sto integrator
   Hform->AddDomainIntegrator(new StabInNavStoIntegrator(mu, *tau, *delta));
   //Hform->SetAssemblyLevel(AssemblyLevel::PARTIAL);

   // Set the essential boundary conditions
   Hform->SetEssentialBC(ess_bdr, rhs);

   // Compute the pressure mass stiffness matrix
   BilinearForm *a = new BilinearForm(spaces[1]);
   ConstantCoefficient one(1.0);
   a->AddDomainIntegrator(new MassIntegrator(one));
   a->Assemble();
   a->Finalize();

   OperatorPtr op;
   Array<int> p_ess_tdofs;
   a->FormSystemMatrix(p_ess_tdofs, op);
   pressure_mass = a->LoseMat();
   delete a;

   // Initialize the Jacobian preconditioner
   JacobianPreconditioner *jac_prec =
      new JacobianPreconditioner(fes, *pressure_mass, block_trueOffsets);
   j_prec = jac_prec;

   // Set up the Jacobian solver
   FGMRESSolver *j_gmres = new FGMRESSolver();
   j_gmres->iterative_mode = false;
   j_gmres->SetRelTol(1e-6);
   j_gmres->SetAbsTol(1e-12);
   j_gmres->SetMaxIter(200);
   j_gmres->SetPrintLevel(-1);
   j_gmres->SetMonitor(j_monitor);
   j_gmres->SetPreconditioner(*j_prec);
   j_solver = j_gmres;

   // Set the newton solve parameters
   newton_solver.iterative_mode = true;
   newton_solver.SetSolver(*j_solver);
   newton_solver.SetOperator(*this);
   newton_solver.SetPrintLevel(-1);
   newton_solver.SetMonitor(newton_monitor);
   newton_solver.SetRelTol(rel_tol);
   newton_solver.SetAbsTol(abs_tol);
   newton_solver.SetMaxIter(iter);
}

// Solve the Newton system
void StabInNavStoOperator::Solve(Vector &xp) const
{
   Vector zero;
   newton_solver.Mult(zero, xp);
  // MFEM_VERIFY(newton_solver.GetConverged(),
  //             "Newton Solver did not converge.");
}

// compute: y = H(x,p)
void StabInNavStoOperator::Mult(const Vector &k, Vector &y) const
{
   adv_gf->SetData(k.GetData());
   Hform->Mult(k, y);
}

// Compute the Jacobian from the nonlinear form
Operator &StabInNavStoOperator::GetGradient(const Vector &xp) const
{
   adv_gf->SetData(xp.GetData());
   return Hform->GetGradient(xp);
}

StabInNavStoOperator::~StabInNavStoOperator()
{
   delete adv_gf;
   delete adv;
   delete tau;
   delete delta;
   delete Hform;
   delete pressure_mass;
   delete j_solver;
   delete j_prec;
}

