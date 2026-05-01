/*******************************************************************\

Module: hw-cbmc Goto-Checker

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#include "hw_cbmc_checker.h"

#include <util/ui_message.h>

#include <trans-word-level/unwind.h>

hw_cbmc_checkert::hw_cbmc_checkert(
  const optionst &options,
  ui_message_handlert &ui_message_handler,
  abstract_goto_modelt &goto_model)
  : multi_path_symex_checkert(options, ui_message_handler, goto_model)
{
}

std::chrono::duration<double>
hw_cbmc_checkert::prepare_property_decider(propertiest &properties)
{
  // Push the C symex equation first.
  auto runtime =
    multi_path_symex_checkert::prepare_property_decider(properties);

  auto &dp = property_decider.get_decision_procedure();

  // Push the Verilog transition relation.
  if(transition_system.has_value() && no_timeframes > 0)
  {
    log.status() << "Unwinding transition system `" << top_module << "' with "
                 << no_timeframes << " time frames" << messaget::eom;
    ::unwind(
      *transition_system,
      ui_message_handler,
      dp,
      no_timeframes,
      ns,
      /*initial_state=*/true);
  }

  // Push the C<->Verilog bridge equality constraints.
  if(!bridge_constraints.empty())
  {
    log.status() << "Adding " << bridge_constraints.size()
                 << " hw-cbmc bridge constraints" << messaget::eom;
    for(const auto &c : bridge_constraints)
      dp.set_to_true(c);
  }

  return runtime;
}
