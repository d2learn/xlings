# Task 03: Store Routing And Install

## Goal

Install packages into the correct store based on the resolved package source.

## Changes

- extend install plan nodes with:
  - namespace
  - canonical package id
  - source scope (`global` / `project`)
  - store root
- route downloads and installs to the correct `xpkgs` root
- reuse existing package files only when `(namespace, name, version)` matches

## Acceptance

- global packages still install into `$XLINGS_HOME/data/xpkgs`
- project packages install into `<project>/.xlings/data/xpkgs`
- installed-path registration uses the correct store

## Tests

- installer path-routing tests
- version registration path tests
