# TLC Model Configuration for TeslaChainAxis.tla

\* CONSTANTS section
CONSTANT
  GenesisHash = "GENESIS"
  MaxHeight = 12

\* INVARIANT section
INVARIANT
  Inv1_AXIS_Contiguity
  Inv2_GENESIS_Immutability
  Inv3_No_Skip_Violations
  Inv4_Chain_Finality

\* SPECIFICATION section  
SPECIFICATION
  Spec

\* DEADLOCK section
\* Disabled — allow system to reach steady state
NO DEADLOCK

\* TEMPORAL properties
PROPERTY
  ConsistencyCheck

\* View — optional, for debugging
VIEW
  <<axisChain, mainChain, violated>>