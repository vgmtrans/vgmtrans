The files in this directory contain the pre-rewrite NinSnes implementation.

They are being kept as the reference behavior while the new NinSnes architecture
is built out in parallel. New work should prefer adding non-legacy components in
the parent `NinSnes/` directory and keep behavior changes out of this subtree
unless needed for mechanical integration.
