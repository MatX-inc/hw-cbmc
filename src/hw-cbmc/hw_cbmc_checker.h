/*******************************************************************\

Module: hw-cbmc Goto-Checker (combines C symex with Verilog transition
        system and the C<->HDL bridge constraints)

Author: Daniel Kroening, kroening@kroening.com

\*******************************************************************/

#ifndef CPROVER_HW_CBMC_HW_CBMC_CHECKER_H
#define CPROVER_HW_CBMC_HW_CBMC_CHECKER_H

#include <util/mathematical_expr.h>

#include <goto-checker/all_properties_verifier_with_trace_storage.h>
#include <goto-checker/multi_path_symex_checker.h>

#include <list>
#include <optional>

class hw_cbmc_checkert : public multi_path_symex_checkert
{
public:
  hw_cbmc_checkert(
    const optionst &,
    ui_message_handlert &,
    abstract_goto_modelt &);

  void set_transition_system(transt ts) { transition_system = std::move(ts); }
  void set_no_timeframes(std::size_t n) { no_timeframes = n; }
  void set_top_module(const irep_idt &m) { top_module = m; }

  void add_constraints(std::list<exprt> cs)
  {
    bridge_constraints.splice(bridge_constraints.end(), cs);
  }

protected:
  std::chrono::duration<double>
  prepare_property_decider(propertiest &) override;

private:
  std::optional<transt> transition_system;
  std::list<exprt> bridge_constraints;
  std::size_t no_timeframes = 0;
  irep_idt top_module;
};

class hw_cbmc_verifiert
  : public all_properties_verifier_with_trace_storaget<hw_cbmc_checkert>
{
public:
  using all_properties_verifier_with_trace_storaget<
    hw_cbmc_checkert>::all_properties_verifier_with_trace_storaget;

  hw_cbmc_checkert &checker() { return incremental_goto_checker; }

  // Repopulate the properties map from the goto model. The wrapper's
  // constructor calls initialize_properties at a point where the goto
  // model hasn't been loaded yet (because hw-cbmc constructs the verifier
  // before get_goto_program). Without this, propagate_fatal_to_proven
  // aborts on assertions that exist in goto_functions but never made it
  // into the properties map.
  void refresh_properties()
  {
    properties = initialize_properties(goto_model);
  }
};

#endif
