
//#include "mfem.hpp"
#include <fstream>
#include <iostream>
#include <random>
//#include "MMA.hpp"
#include "DiffusionSolver.hpp"
//

int main(int argc, char *argv[])
{
   // 1. Initialize MPI
   int num_procs, myrank;
   mfem::MPI_Session mpi(argc, argv);
   // MPI_Init(&argc, &argv);
   // MPI_Comm_size(MPI_COMM_WORLD, &num_procs);
   // MPI_Comm_rank(MPI_COMM_WORLD, &myrank);

   // Define Caliper ConfigManager
#ifdef MFEM_USE_CALIPER
   cali::ConfigManager mgr;
#endif

   // Caliper instrumentation
   MFEM_PERF_FUNCTION;

   // 2. Parse command-line options
   const char *mesh_file = "./ThermalComplianceMesh.g";
   int ser_ref_levels = 1;
   int par_ref_levels = 0;
   int order = 1;
   bool visualization = true;
   double newton_rel_tol = 1e-4;
   double newton_abs_tol = 1e-6;
   int newton_iter = 10;
   int print_level = 0;

   double rel_tol = 1e-7;
   double abs_tol = 1e-15;
   double fradius = 0.05;
   int tot_iter = 100;
   int max_it = 100;

   bool ConstSensFD =false;
   bool ObjSensFD =false;
   bool dQdpFD =false;
   bool dQdsFD =false;
   bool dRdsFD =false;
   bool BreakAfterFirstIt = false;
   bool initializeRandom = false;

   bool initializeSol = false;
   bool restartDesign = false;

   const char *petscrc_file = "";
  
   const char* cali_config = "runtime-report";

   mfem::OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh", "Mesh file to use.");
   args.AddOption(&ser_ref_levels,
                  "-rs",
                  "--refine-serial",
                  "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&par_ref_levels,
                  "-rp",
                  "--refine-parallel",
                  "Number of times to refine the mesh uniformly in parallel.");
   args.AddOption(&order,
                  "-o",
                  "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&visualization,
                  "-vis",
                  "--visualization",
                  "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&newton_rel_tol,
                  "-rel",
                  "--relative-tolerance",
                  "Relative tolerance for the Newton solve.");
   args.AddOption(&newton_abs_tol,
                  "-abs",
                  "--absolute-tolerance",
                  "Absolute tolerance for the Newton solve.");
   args.AddOption(&newton_iter,
                  "-it",
                  "--newton-iterations",
                  "Maximum iterations for the Newton solve.");
   args.AddOption((&print_level), "-prt", "--print-level", "Print level.");
   args.AddOption(&cali_config, "-p", "--caliper",
                  "Caliper configuration string.");
   args.AddOption(&petscrc_file, "-petscopts", "--petscopts",
                     "PetscOptions file to use.");
   args.Parse();
   if (!args.Good())
   {
      if (myrank == 0)
      {
         args.PrintUsage(std::cout);
      }
      MPI_Finalize();
      return 1;
   }
   if (myrank == 0)
   {
      args.PrintOptions(std::cout);
   }
  // mfem::MFEMInitializePetsc(NULL,NULL,petscrc_file,NULL);

   // Caliper configuration
#ifdef MFEM_USE_CALIPER
   mgr.add(cali_config);
   mgr.start();
#endif

   //    Read the (serial) mesh from the given mesh file on all processors. We
   //    can handle triangular, quadrilateral, tetrahedral and hexahedral meshes
   //    with the same code.

   std::string meshName= "ThermalComplianceMesh.g";
   mfem::Mesh *mesh = new mfem::Mesh(meshName.c_str(),0,0);
   int dim = mesh->Dimension();

   // 4. Refine the mesh in serial to increase the resolution. In this example
   //    we do 'ser_ref_levels' of uniform refinement, where 'ser_ref_levels' is
   //    a command-line parameter.
   for (int lev = 0; lev < ser_ref_levels; lev++)
   {
      mesh->UniformRefinement();
   }

   // 5. Define a parallel mesh by a partitioning of the serial mesh. Refine
   //    this mesh further in parallel to increase the resolution. Once the
   //    parallel mesh is defined, the serial mesh can be deleted.
   mfem::ParMesh *pmesh = new mfem::ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;
   for (int lev = 0; lev < par_ref_levels; lev++)
   {
      pmesh->UniformRefinement();
   }

   // Allocate the nonlinear diffusion solver
   mfem::Diffusion_Solver* solver=new mfem::Diffusion_Solver(pmesh,1);

   //add boundary conditions
   solver->AddDirichletBC(1,0.0);


   mfem::ParFiniteElementSpace *fes = solver->GetFES();

      // build h1 desing space
   int orderDesing = 1;
   ::mfem::H1_FECollection desFECol_H1(orderDesing, dim);
   ::mfem::ParFiniteElementSpace desFESpace_scalar_H1(pmesh, &desFECol_H1 );

   //gradients with respect to the desing field
   mfem::Vector objgrad(desFESpace_scalar_H1.GetTrueVSize()); objgrad=0.0; //of the objective
   mfem::Vector volgrad(desFESpace_scalar_H1.GetTrueVSize()); volgrad=0.0; //of the volume contr.

   // set esing variable bounds
   mfem::Vector xxmax(desFESpace_scalar_H1.GetTrueVSize()); 
   mfem::Vector xxmin(desFESpace_scalar_H1.GetTrueVSize()); 

   xxmax=1.0;
   xxmin=1e-4;

   // desing variable vector
   mfem::ParGridFunction desingVarVec(&desFESpace_scalar_H1); desingVarVec=0.5;
   mfem::Vector vdens; vdens.SetSize(desFESpace_scalar_H1.GetTrueVSize()); vdens=0.5;

   if(initializeRandom)
   {
      for(int Ij = 0; Ij< desFESpace_scalar_H1.GetTrueVSize(); Ij++)
      {
         desingVarVec[Ij] = rand() / double(RAND_MAX);
         vdens[Ij] = rand() / double(RAND_MAX);
      }
   }

   desingVarVec.SetFromTrueDofs(vdens);
   //desingVarVec.Print();

   std::cout<<"opt"<<std::endl;

   if(restartDesign)
   {
      std::string tStringIn = "DesingVarVec";
      int n = 6;
      std::string tWorlsRank = std::to_string( mpi.WorldRank());
    
      int precision = n - tWorlsRank.size();
      std::string s = std::string(precision, '0').append(tWorlsRank);

      tStringIn= tStringIn +"."+s;

      std::ifstream inp(tStringIn);
      mfem::ParGridFunction tLoadGF(pmesh, inp);

      desingVarVec = tLoadGF;

      for( int Ik=0;Ik<desingVarVec.Size();Ik++)
      {
         if(desingVarVec[Ik] < 0.1){desingVarVec[Ik]=0.1;}
      }
   }

   double max_ch=0.01; //max design change

   double ThermalCompliance; //energy dissipation  // cpl
   double vol; //volume

   double max_vol = 1.0;
   double maxVolAllowed = max_vol*0.5;

   mfem::ParaViewDataCollection paraview_dc("TopOpt", pmesh);
   paraview_dc.SetPrefixPath("ParaView");
   paraview_dc.SetLevelsOfDetail(order);
   paraview_dc.SetDataFormat(mfem::VTKFormat::BINARY);
   paraview_dc.SetHighOrderOutput(true);

   solver->SetLinearSolver(1e-10, 1e-12, 1000);

   if(true)
   {
      std::cout<<"opt iter"<<std::endl;

      for(int i=1;i<max_it;i++)
      {
         //solve desing variable and solve
         solver->SetDesignGF(&desingVarVec);

         solver->FSolve();

         mfem::ParGridFunction TempGF;
         solver->GetSol(TempGF);

         // evaluate obj and volume
         mfem::ThermalComplianceQoI tObj;
         tObj.SetFESpaceAndField(fes, &desingVarVec,  &TempGF);

         std::cout<<"obj"<<std::endl;
         mfem::VolumeQoI tContraint;
         tContraint.SetDesignFES( &desFESpace_scalar_H1 );
         tContraint.SetDesField( &desingVarVec );

         std::cout<<"val"<<std::endl;
         ThermalCompliance =tObj.Eval();
         vol               =tContraint.Eval();

         if(myrank==0)
         {
             std::cout<<"it: "<<i<<" | obj= "<<ThermalCompliance<<" | vol= "<<vol<<" | Constraint: "<< vol-maxVolAllowed<<std::endl;
         }

         tObj      .Grad(objgrad);     //dQ/dT   explicit
         tContraint.Grad(volgrad);     //dV/dT   explicit

         std::cout<<"EVAL: "<<std::endl;


         mfem::ThermalComplianceIntegrator_1 * ImpdQdTIntegrator = new mfem::ThermalComplianceIntegrator_1;
         ImpdQdTIntegrator->SetDesignAndTempGF(&desingVarVec, &TempGF);
         mfem::ParLinearForm ParLinerFormDQDT(fes);
         ParLinerFormDQDT.AddDomainIntegrator(ImpdQdTIntegrator);

         ParLinerFormDQDT.Assemble();                             // dQ/dT       adjoint load

         //ParLinerFormDQDT.Print();         
         solver->ASolve(ParLinerFormDQDT);                       // adjoint solve    dR/dT^(-T) * dQ/dT = lambda

         mfem::ParGridFunction tAdjointGF;
         solver->GetAdj(tAdjointGF);

         std::cout<<"Adjoint Solve done"<<std::endl;

         // ---- posmultiply -----

         mfem::DiffusionAdjointPostIntegrator * AdjointPostIntegrator = new mfem::DiffusionAdjointPostIntegrator;
         AdjointPostIntegrator->SetAdjoint(&tAdjointGF);
         AdjointPostIntegrator->SetDesignAndTempGF(&desingVarVec, &TempGF);
         mfem::ParLinearForm ParLinerFormPostAdjoint(&desFESpace_scalar_H1);
         ParLinerFormPostAdjoint.AddDomainIntegrator(AdjointPostIntegrator);

         ParLinerFormPostAdjoint.Assemble();                    // lambda * dR/ds


         objgrad -= ParLinerFormPostAdjoint;                     // dQ/ds(explicit) - labda * dR/ds

         paraview_dc.SetCycle(i);
         paraview_dc.SetTime(i*1.0);
         paraview_dc.RegisterField("design",&desingVarVec);
         mfem::ParGridFunction objGradGF(&desFESpace_scalar_H1); objGradGF = objgrad;
         paraview_dc.RegisterField("ObjGrad",&objGradGF);
         paraview_dc.RegisterField("Temp",&TempGF);
         paraview_dc.Save();


         // FIXME TIM
         {
              // impose desing variable bounds - set xxmin and xxmax
              xxmin=desingVarVec; xxmin-=max_ch;
              xxmax=desingVarVec; xxmax+=max_ch;
              for(int li=0;li<xxmin.Size();li++)
              {
                  if(xxmin[li]<0.1)
                  {
                     xxmin[li]=0.1;
                  }
                  if(xxmax[li]>0.45)
                  {
                     xxmax[li]=0.45;
                  }
              }
         }
         
         
         // if(ConstSensFD)
         // {
         //    double epsilon = 1e-8;
         //    mfem::ParGridFunction tFD_sens(&desFESpace_scalar_H1); tFD_sens = 0.0;
         //    for( int Ia = 0; Ia<desingVarVec.Size(); Ia++)
         //    {
         //       desingVarVec[Ia] +=epsilon;

         //       mfem::VolumeQoI tContraintFD_1;
         //       tContraintFD_1.SetDesignFES( &desFESpace_scalar_H1 );
         //       tContraintFD_1.SetDesField( desingVarVec );
         //       double volFD_1  =tContraint.Eval();

         //       desingVarVec[Ia] -=2.0*epsilon;
         //       mfem::VolumeQoI tContraintFD_2;
         //       tContraintFD_2.SetDesignFES( &desFESpace_scalar_H1 );
         //       tContraintFD_2.SetDesField( desingVarVec );
         //       double volFD_2  =tContraint.Eval();

         //       desingVarVec[Ia] +=epsilon;

         //       tFD_sens[Ia] = (volFD_1-volFD_2)/(2.0*epsilon);
         //    }//
         //       volgrad.Print();
         //       std::cout<<"  ----------  FD Diff ------------"<<std::endl;
         //      // tFD_sens.Print();

         //       std::cout<<"  ---------- Analytic - FD Diff ------------"<<std::endl;
         //       mfem::ParGridFunction tFD_diff(&desFESpace_scalar_H1); tFD_diff = 0.0;
         //       tFD_diff = volgrad;
         //       tFD_diff -=tFD_sens;
         //       //tFD_diff.Print();

         //          std::cout<<"ConstSensFD norm: "<<tFD_diff.Norml2()<<std::endl;
         // }

         // if(ObjSensFD)
         // {
         //    double epsilon = 1e-8;
         //    mfem::ParGridFunction tFD_sens(&desFESpace_scalar_H1); tFD_sens = 0.0;
         //    for( int Ia = 0; Ia<desingVarVec.Size(); Ia++)
         //    {
         //          desingVarVec[Ia] +=epsilon;

         //          // Allocate the nonlinear diffusion solver
         //          mfem::NLDiffusion solver_FD1(pmesh,2);
         //          //solver_FD1.AddDirichletBC(2,0.05);
         //          solver_FD1.AddDirichletBC(3,0.0);
         //          solver_FD1.AddNeumannLoadVal( tLoad, Attribute );
         //          solver_FD1.AddMaterial(tMatCoeff);
         //          solver_FD1.AddDesignCoeff(DesingCoeff);
         //          solver_FD1.AddDesignGF(&desingVarVec);
         //          solver_FD1.SetNewtonSolver(1e-6, 1e-8,15, 1, 1.0);
         //          solver_FD1.SetLinearSolver(1e-10, 1e-12, 1000, 0);
                  
         //          solver_FD1.FSolve();
                  

         //          // mfem::ParGridFunction tPreassureGF_FD;
         //          // solver->GetSol(tPreassureGF_FD);

         //          mfem::EnergyDissipationObjective tObjFD_1;
         //          tObjFD_1.SetNLDiffusionSolver( &solver_FD1 );
         //          tObjFD_1.SetDesignFES( &desFESpace_scalar_H1 );
         //          tObjFD_1.SetDesField( desingVarVec );
         //          tObjFD_1.SetNLDiffusionCoeff( tMatCoeff );
         //          double energyDissipFD_1 =tObjFD_1.Eval();

         //          desingVarVec[Ia] -=2.0*epsilon;
         //          mfem::NLDiffusion solver_FD2(pmesh,2);
         //          //solver_FD2.AddDirichletBC(2,0.05);
         //          solver_FD2.AddDirichletBC(3,0.0);
         //          solver_FD2.SetNewtonSolver(1e-6, 1e-8,15, 1, 1.0);
         //          solver_FD2.SetLinearSolver(1e-10, 1e-12, 1000, 0);
         //          solver_FD2.AddNeumannLoadVal( tLoad, Attribute );
         //          solver_FD2.AddMaterial(tMatCoeff);
         //          solver_FD2.AddDesignCoeff(DesingCoeff);
         //          solver_FD2.AddDesignGF(&desingVarVec);
         //          solver_FD2.FSolve();
         //          // mfem::ParGridFunction tPreassureGF_FD;
         //          // solver->GetSol(tPreassureGF_FD);

         //          mfem::EnergyDissipationObjective tObjFD_2;
         //          tObjFD_2.SetNLDiffusionSolver( &solver_FD2 );
         //          tObjFD_2.SetDesignFES( &desFESpace_scalar_H1 );
         //          tObjFD_2.SetDesField( desingVarVec );
         //          tObjFD_2.SetNLDiffusionCoeff(tMatCoeff );
         //          double energyDissipFD_2 =tObjFD_2.Eval();

         //          desingVarVec[Ia] +=epsilon;

         //          tFD_sens[Ia] = (energyDissipFD_1-energyDissipFD_2)/(2.0*epsilon);
         //          std::cout<<"Var number: "<< Ia<< " Analytic: "<<objgrad[Ia] << " FD: "<< tFD_sens[Ia]<<std::endl;
         //    }
            
         //   // objgrad.Print();
         //    std::cout<<"  ---------- FD obj ------------"<<std::endl;
         //   // tFD_sens.Print();
         //    std::cout<<"  ---------- Analytic - FD Diff ------------"<<std::endl;
         //    mfem::ParGridFunction tFD_diff(&desFESpace_scalar_H1); tFD_diff = 0.0;
         //    tFD_diff = objgrad;
         //    tFD_diff -=tFD_sens;
         //    //tFD_diff.Print();

         //    std::cout<<"norml2: "<<tFD_diff.Norml2()<<"normllinf: "<<tFD_diff.Normlinf()<<"max/min: "<<tFD_diff.Max()<<" / "<<tFD_diff.Min()<<std::endl;
         // }

         // if(dQdTFD)
         // {
         //    double epsilon = 1e-8;
         //    mfem::ParGridFunction tFD_sens(fes); tFD_sens = 0.0;
         //    for( int Ia = 0; Ia<tPreassureGF.Size(); Ia++)
         //    {
         //       tPreassureGF[Ia] +=epsilon;

         //       mfem::EnergyDissipationObjective tObj_FD1;
         //       tObj_FD1.SetNLDiffusionSolver( solver );
         //       tObj_FD1.SetPreassure( &tPreassureGF);
         //       tObj_FD1.SetDesignFES( fes );
         //       tObj_FD1.SetDesField( desingVarVec );
         //       tObj_FD1.SetNLDiffusionCoeff( tMatCoeff );

         //       double energyDissipFD1 =tObj_FD1.Eval();

         //       tPreassureGF[Ia] -=2.0*epsilon;
               
         //       mfem::EnergyDissipationObjective tObj_FD2;
         //       tObj_FD2.SetNLDiffusionSolver( solver );
         //       tObj_FD2.SetPreassure( &tPreassureGF);
         //       tObj_FD2.SetDesignFES( fes );
         //       tObj_FD2.SetDesField( desingVarVec );
         //       tObj_FD2.SetNLDiffusionCoeff( tMatCoeff );

         //       double energyDissipFD2 =tObj_FD2.Eval();

         //       tPreassureGF[Ia] +=epsilon;

         //       tFD_sens[Ia] = (energyDissipFD1-energyDissipFD2)/(2.0*epsilon);
         //    }
         //       ParLinerFormDQDp.Print();
         //       std::cout<<"  ----------  FD Diff ------------"<<std::endl;
         //       tFD_sens.Print();

         //       std::cout<<"  ---------- Analytic - FD Diff ------------"<<std::endl;
         //       mfem::ParGridFunction tFD_diff(fes); tFD_diff = 0.0;
         //       tFD_diff = ParLinerFormDQDp;
         //       tFD_diff -=tFD_sens;
         //       tFD_diff.Print();
         //                               std::cout<<"norm: "<<tFD_diff.Norml2()<<std::endl;
         // }

         // if(dQdsFD)
         // {
         //    double epsilon = 1e-8;
         //    mfem::ParGridFunction tFD_sens(fes); tFD_sens = 0.0;
         //    for( int Ia = 0; Ia<desingVarVec.Size(); Ia++)
         //    {
         //       desingVarVec[Ia] +=epsilon;

         //       mfem::EnergyDissipationObjective tObj_FD1;
         //       tObj_FD1.SetNLDiffusionSolver( solver );
         //       tObj_FD1.SetPreassure( &tPreassureGF);
         //       tObj_FD1.SetDesignFES( fes );
         //       tObj_FD1.SetDesField( desingVarVec );
         //       tObj_FD1.SetNLDiffusionCoeff( tMatCoeff );

         //       double energyDissipFD1 =tObj_FD1.Eval();

         //       desingVarVec[Ia] -=2.0*epsilon;
               
         //       mfem::EnergyDissipationObjective tObj_FD2;
         //       tObj_FD2.SetNLDiffusionSolver( solver );
         //       tObj_FD2.SetPreassure( &tPreassureGF);
         //       tObj_FD2.SetDesignFES( fes );
         //       tObj_FD2.SetDesField( desingVarVec );
         //       tObj_FD2.SetNLDiffusionCoeff( tMatCoeff );

         //       double energyDissipFD2 =tObj_FD2.Eval();

         //       desingVarVec[Ia] +=epsilon;

         //       tFD_sens[Ia] = (energyDissipFD1-energyDissipFD2)/(2.0*epsilon);
         //    }
         //       objgrad.Print();
         //       std::cout<<"  ----------  FD Diff ------------"<<std::endl;
         //       tFD_sens.Print();

         //       std::cout<<"  ---------- Analytic - FD Diff ------------"<<std::endl;
         //       mfem::ParGridFunction tFD_diff(fes); tFD_diff = 0.0;
         //       tFD_diff = objgrad;
         //       tFD_diff -=tFD_sens;
         //       tFD_diff.Print();
         //                               std::cout<<"norm: "<<tFD_diff.Norml2()<<std::endl;
         // }

         // if(dRdsFD)
         // {
         //    double epsilon = 1e-8;
         //    mfem::ParGridFunction tFD_sens(fes); tFD_sens = 0.0;
         //    for( int Ia = 0; Ia<desingVarVec.Size(); Ia++)
         //    {
         //       desingVarVec[Ia] +=epsilon;

         //       mfem::EnergyDissipationObjective tObj_FD1;
         //       tObj_FD1.SetNLDiffusionSolver( solver );
         //       tObj_FD1.SetPreassure( &tPreassureGF);
         //       tObj_FD1.SetDesignFES( fes );
         //       tObj_FD1.SetDesField( desingVarVec );
         //       tObj_FD1.SetNLDiffusionCoeff( tMatCoeff );

         //       double energyDissipFD1 =tObj_FD1.Eval();

         //       desingVarVec[Ia] -=2.0*epsilon;
               
         //       mfem::EnergyDissipationObjective tObj_FD2;
         //       tObj_FD2.SetNLDiffusionSolver( solver );
         //       tObj_FD2.SetPreassure( &tPreassureGF);
         //       tObj_FD2.SetDesignFES( fes );
         //       tObj_FD2.SetDesField( desingVarVec );
         //       tObj_FD2.SetNLDiffusionCoeff( tMatCoeff );

         //       double energyDissipFD2 =tObj_FD2.Eval();

         //       desingVarVec[Ia] +=epsilon;

         //       tFD_sens[Ia] = (energyDissipFD1-energyDissipFD2)/(2.0*epsilon);
         //    }
         //       objgrad.Print();
         //       std::cout<<"  ----------  FD Diff ------------"<<std::endl;
         //       tFD_sens.Print();

         //       std::cout<<"  ---------- Analytic - FD Diff ------------"<<std::endl;
         //       mfem::ParGridFunction tFD_diff(fes); tFD_diff = 0.0;
         //       tFD_diff = objgrad;
         //       tFD_diff -=tFD_sens;
         //       tFD_diff.Print();
         //                               std::cout<<"norm: "<<tFD_diff.Norml2()<<std::endl;
         // }

         if(BreakAfterFirstIt)
         {
             mfem::mfem_error("break before update");
         }

         // FIXME TIM

         double con=vol/maxVolAllowed-1;                                      // V/V_max -1
         //mma->Update(desingVarVec,objgrad,&con,&volgrad,xxmin,xxmax);

         std::string tDesingName = "DesingVarVec";
         desingVarVec.Save( tDesingName.c_str() );

         std::string tFieldName = "FieldVec";
         TempGF.Save( tFieldName.c_str() );

      }

   }


   // delete mma;
    //delete DesingCoeff;


   delete solver;
   delete pmesh;
   // Flush output before MPI_finalize
#ifdef MFEM_USE_CALIPER
   mgr.flush();
#endif
   MPI_Finalize();

   return 0;
}

