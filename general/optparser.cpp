// Copyright (c) 2010-2025, Lawrence Livermore National Security, LLC. Produced
// at the Lawrence Livermore National Laboratory. All Rights reserved. See files
// LICENSE and NOTICE for details. LLNL-CODE-806117.
//
// This file is part of the MFEM library. For more information and source code
// availability visit https://mfem.org.
//
// MFEM is free software; you can redistribute it and/or modify it under the
// terms of the BSD-3 license. We welcome feedback and contributions, see file
// CONTRIBUTING.md for details.

#include "optparser.hpp"
#include "../linalg/vector.hpp"
#include "../general/communication.hpp"
#include <cctype>

namespace mfem
{

using namespace std;

int isValidAsInt(char * s)
{
   if ( s == NULL || *s == '\0' )
   {
      return 0;   // Empty string
   }

   if ( *s == '+' || *s == '-' )
   {
      ++s;
   }

   if ( *s == '\0')
   {
      return 0;   // sign character only
   }

   while (*s)
   {
      if ( !isdigit(*s) )
      {
         return 0;
      }
      ++s;
   }

   return 1;
}

int isValidAsDouble(char * s)
{
   // A valid floating point number for atof using the "C" locale is formed by
   // - an optional sign character (+ or -),
   // - followed by a sequence of digits, optionally containing a decimal-point
   //   character (.),
   // - optionally followed by an exponent part (an e or E character followed by
   //   an optional sign and a sequence of digits).

   if ( s == NULL || *s == '\0' )
   {
      return 0;   // Empty string
   }

   if ( *s == '+' || *s == '-' )
   {
      ++s;
   }

   if ( *s == '\0')
   {
      return 0;   // sign character only
   }

   while (*s)
   {
      if (!isdigit(*s))
      {
         break;
      }
      ++s;
   }

   if (*s == '\0')
   {
      return 1;   // s = "123"
   }

   if (*s == '.')
   {
      ++s;
      while (*s)
      {
         if (!isdigit(*s))
         {
            break;
         }
         ++s;
      }
      if (*s == '\0')
      {
         return 1;   // this is a fixed point double s = "123." or "123.45"
      }
   }

   if (*s == 'e' || *s == 'E')
   {
      ++s;
      return isValidAsInt(s);
   }
   else
   {
      return 0;   // we have encounter a wrong character
   }
}

void parseArray(char * str, Array<int> & var)
{
   var.SetSize(0);
   std::stringstream input(str);
   int val;
   while ( input >> val)
   {
      var.Append(val);
   }
}

void parseVector(char * str, Vector & var)
{
   int nentries = 0;
   real_t val;
   {
      std::stringstream input(str);
      while ( input >> val)
      {
         ++nentries;
      }
   }

   var.SetSize(nentries);
   {
      nentries = 0;
      std::stringstream input(str);
      while ( input >> val)
      {
         var(nentries++) = val;
      }
   }
}

void OptionsParser::Parse()
{
   option_check.SetSize(options.Size());
   option_check = 0;
   for (int i = 1; i < argc; )
   {
      if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
      {
         // print help message
         error_type = 1;
         return;
      }

      for (int j = 0; true; j++)
      {
         if (j >= options.Size())
         {
            // unrecognized option
            error_type = 2;
            error_idx = i;
            return;
         }

         if (strcmp(argv[i], options[j].short_name) == 0 ||
             strcmp(argv[i], options[j].long_name) == 0)
         {
            OptionType type = options[j].type;

            if ( option_check[j] )
            {
               error_type = 4;
               error_idx = j;
               return;
            }
            option_check[j] = 1;

            i++;
            if (type != ENABLE && type != DISABLE && i >= argc)
            {
               // missing argument
               error_type = 3;
               error_idx = j;
               return;
            }

            int isValid = 1;
            switch (options[j].type)
            {
               case INT:
                  isValid = isValidAsInt(argv[i]);
                  *(int *)(options[j].var_ptr) = atoi(argv[i++]);
                  break;
               case DOUBLE:
                  isValid = isValidAsDouble(argv[i]);
                  *(real_t *)(options[j].var_ptr) = atof(argv[i++]);
                  break;
               case STRING:
                  *(const char **)(options[j].var_ptr) = argv[i++];
                  break;
               case STD_STRING:
                  *(std::string *)(options[j].var_ptr) = argv[i++];
                  break;
               case ENABLE:
                  *(bool *)(options[j].var_ptr) = true;
                  option_check[j+1] = 1;  // Do not allow the DISABLE Option
                  break;
               case DISABLE:
                  *(bool *)(options[j].var_ptr) = false;
                  option_check[j-1] = 1;  // Do not allow the ENABLE Option
                  break;
               case ARRAY:
                  parseArray(argv[i++], *(Array<int>*)(options[j].var_ptr) );
                  break;
               case VECTOR:
                  parseVector(argv[i++], *(Vector*)(options[j].var_ptr) );
                  break;
            }

            if (!isValid)
            {
               error_type = 5;
               error_idx = i;
               return;
            }

            break;
         }
      }
   }

   // check for missing required options
   for (int i = 0; i < options.Size(); i++)
      if (options[i].required &&
          (option_check[i] == 0 ||
           (options[i].type == ENABLE && option_check[++i] == 0)))
      {
         error_type = 6; // required option missing
         error_idx = i; // for a boolean option i is the index of DISABLE
         return;
      }

   error_type = 0;
}

void OptionsParser::ParseCheck(std::ostream &os)
{
   Parse();
   int my_rank = 0;
#ifdef MFEM_USE_MPI
   int mpi_is_initialized = Mpi::IsInitialized();
   if (mpi_is_initialized) { my_rank = Mpi::WorldRank(); }
#endif
   if (!Good())
   {
      if (my_rank == 0) { PrintUsage(os); }
#ifdef MFEM_USE_MPI
      Mpi::Finalize();
#endif
      std::exit(1);
   }
   if (my_rank == 0) { PrintOptions(os); }
}

void OptionsParser::WriteValue(const Option &opt, std::ostream &os)
{
   switch (opt.type)
   {
      case INT:
         os << *(int *)(opt.var_ptr);
         break;

      case DOUBLE:
         os << *(real_t *)(opt.var_ptr);
         break;

      case STRING:
         os << *(const char **)(opt.var_ptr);
         break;

      case STD_STRING:
         out << *(std::string *)(opt.var_ptr);
         break;

      case ARRAY:
      {
         Array<int> &list = *(Array<int>*)(opt.var_ptr);
         os << '\'';
         if (list.Size() > 0)
         {
            os << list[0];
         }
         for (int i = 1; i < list.Size(); i++)
         {
            os << ' ' << list[i];
         }
         os << '\'';
         break;
      }

      case VECTOR:
      {
         Vector &list = *(Vector*)(opt.var_ptr);
         os << '\'';
         if (list.Size() > 0)
         {
            os << list(0);
         }
         for (int i = 1; i < list.Size(); i++)
         {
            os << ' ' << list(i);
         }
         os << '\'';
         break;
      }

      default: // provide a default to suppress warning
         break;
   }
}

void OptionsParser::PrintOptions(ostream &os) const
{
   static const char *indent = "   ";

   os << "Options used:\n";
   for (int j = 0; j < options.Size(); j++)
   {
      OptionType type = options[j].type;

      os << indent;
      if (type == ENABLE)
      {
         if (*(bool *)(options[j].var_ptr) == true)
         {
            os << options[j].long_name;
         }
         else
         {
            os << options[j+1].long_name;
         }
         j++;
      }
      else
      {
         os << options[j].long_name << " ";
         WriteValue(options[j], os);
      }
      os << '\n';
   }
}

void OptionsParser::PrintError(ostream &os) const
{
   static const char *line_sep = "";

   os << line_sep;
   switch (error_type)
   {
      case 2:
         os << "Unrecognized option: " << argv[error_idx] << '\n' << line_sep;
         break;

      case 3:
         os << "Missing argument for the last option: " << argv[argc-1]
            << '\n' << line_sep;
         break;

      case 4:
         if (options[error_idx].type == ENABLE )
            os << "Option " << options[error_idx].long_name << " or "
               << options[error_idx + 1].long_name
               << " provided multiple times\n" << line_sep;
         else if (options[error_idx].type == DISABLE)
            os << "Option " << options[error_idx - 1].long_name << " or "
               << options[error_idx].long_name
               << " provided multiple times\n" << line_sep;
         else
            os << "Option " << options[error_idx].long_name
               << " provided multiple times\n" << line_sep;
         break;

      case 5:
         os << "Wrong option format: " << argv[error_idx - 1] << " "
            << argv[error_idx] << '\n' << line_sep;
         break;

      case 6:
         os << "Missing required option: " << options[error_idx].long_name
            << '\n' << line_sep;
         break;
   }
   os << endl;
}

void OptionsParser::PrintHelp(ostream &os) const
{
   static const char *indent = "   ";
   static const char *seprtr = ", ";
   static const char *descr_sep = "\n\t";
   static const char *line_sep = "";
   static const char *types[] = { " <int>", " <double>", " <string>",
                                  " <string>", "", "", " '<int>...'",
                                  " '<double>...'"
                                };

   os << indent << "-h" << seprtr << "--help" << descr_sep
      << "Print this help message and exit.\n" << line_sep;
   for (int j = 0; j < options.Size(); j++)
   {
      OptionType type = options[j].type;

      os << indent << options[j].short_name << types[type]
         << seprtr << options[j].long_name << types[type]
         << seprtr;
      if (options[j].required)
      {
         os << "(required)";
      }
      else
      {
         if (type == ENABLE)
         {
            j++;
            os << options[j].short_name << types[type] << seprtr
               << options[j].long_name << types[type] << seprtr
               << "current option: ";
            if (*(bool *)(options[j].var_ptr) == true)
            {
               os << options[j-1].long_name;
            }
            else
            {
               os << options[j].long_name;
            }
         }
         else
         {
            os << "current value: ";
            WriteValue(options[j], os);
         }
      }
      os << descr_sep;

      if (options[j].description)
      {
         os << options[j].description << '\n';
      }
      os << line_sep;
   }
}

void OptionsParser::PrintUsage(ostream &os) const
{
   static const char *line_sep = "";

   PrintError(os);
   os << "Usage: " << argv[0] << " [options] ...\n" << line_sep
      << "Options:\n" << line_sep;
   PrintHelp(os);
}

}
