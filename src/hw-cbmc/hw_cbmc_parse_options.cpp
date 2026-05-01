/*******************************************************************\

Module: Main Module 

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "hw_cbmc_parse_options.h"

#include <util/config.h>
#include <util/exit_codes.h>
#include <util/get_module.h>
#include <util/string2int.h>
#include <util/unicode.h>
#include <util/version.h>

#include <goto-programs/set_properties.h>
#include <goto-programs/show_properties.h>

#include <ansi-c/goto-conversion/goto_convert_functions.h>
#include <ebmc/show_modules.h>
#include <langapi/mode.h>
#include <linking/static_lifetime_init.h>

#include "gen_interface.h"
#include "hw_cbmc_checker.h"
#include "map_vars.h"

#include <iostream>

/*******************************************************************\

Function: hw_cbmc_parse_optionst::doit

  Inputs:

 Outputs:

 Purpose: invoke main modules

\*******************************************************************/

int hw_cbmc_parse_optionst::doit()
{
  if(cmdline.isset("version"))
  {
    std::cout << CBMC_VERSION << std::endl;
    return 0;
  }

  //
  // command line options
  //

  optionst options;
  get_command_line_options(options);

  messaget::eval_verbosity(cmdline.get_value("verbosity"),
                           messaget::M_STATISTICS, ui_message_handler);

  //
  // Print a banner
  //
  log.status() << "HW-CBMC version " << CBMC_VERSION << messaget::eom;

  register_languages();

  if(cmdline.isset("preprocess"))
  {
    preprocessing(options);
    return 0;
  }

  if(cmdline.isset("vcd"))
    options.set_option("vcd", cmdline.get_value("vcd"));

  std::unique_ptr<hw_cbmc_verifiert> verifier = nullptr;
  verifier = std::make_unique<hw_cbmc_verifiert>(
    options, ui_message_handler, goto_model);

  int get_goto_program_ret =
      get_goto_program(goto_model, options, cmdline, ui_message_handler);
  if (get_goto_program_ret != -1)
    return get_goto_program_ret;

  std::list<exprt> constraints;
  int get_modules_ret = get_modules(constraints);
  if (get_modules_ret != -1)
    return get_modules_ret;

  goto_convert(goto_model.symbol_table, goto_model.goto_functions,
               ui_message_handler);
  if (cbmc_parse_optionst::process_goto_program(goto_model, options, log))
    return CPROVER_EXIT_INTERNAL_ERROR;

  // map_vars added new static-lifetime symbols (top_array, hw-cbmc::timeframe)
  // and updated existing ones (notably setting top.value = top_array[0] and
  // bound = no_timeframes-1). The CPROVER_initialize function was generated
  // by get_goto_program before these updates, so regenerate it now from the
  // (now-updated) symbol values.
  recreate_initialize_function(goto_model, ui_message_handler);

  // Hand the Verilog transition system and the C<->HDL bridge constraints
  // to the checker so it can inject them into the same decision procedure
  // that holds the C-side symex equation.
  {
    irep_idt top_module = get_top_module();
    std::size_t no_timeframes = get_bound();

    if (!top_module.empty() && no_timeframes > 0) {
      namespacet ns(goto_model.symbol_table);
      verifier->checker().set_top_module(top_module);
      verifier->checker().set_no_timeframes(no_timeframes);
      verifier->checker().set_transition_system(
        to_trans_expr(ns.lookup(top_module).value));
    }

    verifier->checker().add_constraints(std::move(constraints));
  }

  label_properties(goto_model.goto_functions);

  // Now that goto_model is fully populated and labelled, repopulate the
  // verifier's properties map (the wrapper's constructor seeded it with
  // an empty model since we constructed the verifier early).
  verifier->refresh_properties();

  if (cmdline.isset("show-properties")) {
    show_properties(goto_model, ui_message_handler);
    return 0;
  }
  if (set_properties())
    return 7;

  // do actual BMC
  const resultt result = (*verifier)();
  verifier->report();
  return result_to_exit_code(result);
}

/*******************************************************************\

Function: hw_cbmc_parse_optionst::get_top_module

  Inputs:

 Outputs:

 Purpose: add additional (synchonous) modules

\*******************************************************************/

irep_idt hw_cbmc_parse_optionst::get_top_module()
{
  std::string top_module;

  if(cmdline.isset("module"))
    top_module=cmdline.get_value("module");
  else if(cmdline.isset("top"))
    top_module=cmdline.get_value("top");

  if(top_module=="")
    return irep_idt();

  return get_module(goto_model.symbol_table, top_module, ui_message_handler)
      .name;
}

/*******************************************************************\

Function: hw_cbmc_parse_optionst::get_bound

  Inputs:

 Outputs:

 Purpose: add additional (synchonous) modules

\*******************************************************************/

unsigned hw_cbmc_parse_optionst::get_bound()
{
  if(cmdline.isset("bound"))
    return safe_string2unsigned(cmdline.get_value("bound"))+1;
  else
    return 1;
}

/*******************************************************************\

Function: hw_cbmc_parse_optionst::get_modules

  Inputs:

 Outputs:

 Purpose: add additional (synchonous) modules

\*******************************************************************/

int hw_cbmc_parse_optionst::get_modules(std::list<exprt> &bmc_constraints) {
  //
  // unwinding of transition systems
  //

  irep_idt top_module=get_top_module();

  if(!top_module.empty())
  {
    try
    {
      if(cmdline.isset("gen-interface"))
      {
        const symbolt &symbol =
            namespacet(goto_model.symbol_table).lookup(top_module);

        if(cmdline.isset("outfile"))
        {
          std::ofstream out(widen_if_needed(cmdline.get_value("outfile")));
          if(!out)
          {
            log.error() << "failed to open given outfile" << messaget::eom;
            return 6;
          }

          gen_interface(goto_model.symbol_table, symbol, true, out, std::cerr);
        }
        else
          gen_interface(goto_model.symbol_table, symbol, true, std::cout, std::cerr);

        return 0; // done
      }
      
      //
      // map HDL variables to C variables
      //

      log.status() << "Mapping variables" << messaget::eom;

      map_vars(goto_model.symbol_table, top_module, bmc_constraints,
               ui_message_handler, get_bound());
    }

    catch(int e) { return 6; }
  }
  else if(cmdline.isset("gen-interface"))
  {
    log.error() << "must specify top module name for gen-interface"
                << messaget::eom;
    return 6;
  }
  else if(cmdline.isset("show-modules"))
  {
    show_modulest::from_symbol_table(goto_model.symbol_table)
      .plain_text(std::cout);
    return 0; // done
  }
    
  return -1; // continue
}

/*******************************************************************\

Function: hw_cbmc_parse_optionst::help

  Inputs:

 Outputs:

 Purpose: display command line help

\*******************************************************************/

void hw_cbmc_parse_optionst::help()
{
  std::cout <<
    "* *  hw-cbmc is protected in part by U.S. patent 7,225,417  * *";

  cbmc_parse_optionst::help();

  std::cout <<
    "hw-cbmc also accepts the following options:\n"
    " --module name                top module for unwinding (deprecated)\n"
    " --top name                   top module for unwinding\n"
    " --bound nr                   number of transitions for the module\n"
    " --gen-interface              print C for interface to module\n"
    " --vcd file                   dump error trace in VCD format\n"
    "\n";
}
