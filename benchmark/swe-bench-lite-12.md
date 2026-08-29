# SWE-bench Lite — 12 tasks for trim+henry-guide vs no-trim benchmark

Fixed 12 tasks from `princeton-nlp/SWE-bench_Lite` (test split). Each arm (skill+trim vs no-skill+no-trim) fixes all 12 in one session; compare tokens/context used and pass rate.

Selection rule (score, higher = more room for token savings):
1. changed file **not named in the issue** -> agent must search the codebase (trim's `rg`/`outline`/`sed` win over whole-file dumps)
2. small fix (`+` lines small, <=3 files) -> waste is in *finding*, not writing
3. large repo (django/sympy/matplotlib/scikit-learn) -> more files, bigger reads to waste tokens on
4. focused FAIL_TO_PASS target -> same result, less test-run noise

## 1. django__django-11564 — Add support for SCRIPT_NAME in STATIC_URL and MEDIA_URL
- repo: django/django
- changed: django/conf/__init__.py
- FAIL_TO_PASS: test_add_script_name_prefix (settings_tests.tests.MediaURLStaticURLPrefixTest), test_not_prefixed (settings_tests.tests.MediaURLStaticURLPrefixTest)
- why: file not named in issue -> search required; tiny fix (+30 lines)

## 2. scikit-learn__scikit-learn-11281 — Should mixture models have a clusterer-compatible interface
- repo: scikit-learn/scikit-learn
- changed: sklearn/mixture/base.py
- FAIL_TO_PASS: sklearn/mixture/tests/test_bayesian_mixture.py::test_bayesian_mixture_fit_predict, sklearn/mixture/tests/test_gaussian_mixture.py::test_gaussian_mixture_fit_predict
- why: file not named in issue -> search required; tiny fix (+28 lines)

## 3. scikit-learn__scikit-learn-25500 — CalibratedClassifierCV doesn't work with `set_config(transform_output="pandas")`
- repo: scikit-learn/scikit-learn
- changed: sklearn/isotonic.py
- FAIL_TO_PASS: sklearn/tests/test_isotonic.py::test_isotonic_regression_output_predict
- why: file not named in issue -> search required; tiny fix (+26 lines)

## 4. django__django-11283 — Migration auth.0011_update_proxy_permissions fails for models recreated as a proxy.
- repo: django/django
- changed: django/contrib/auth/migrations/0011_update_proxy_permissions.py
- FAIL_TO_PASS: test_migrate_with_existing_target_permission (auth_tests.test_migrations.ProxyModelWithSameAppLabelTests)
- why: file not named in issue -> search required; tiny fix (+25 lines)

## 5. sympy__sympy-21055 — `refine()` does not understand how to simplify complex arguments
- repo: sympy/sympy
- changed: sympy/assumptions/refine.py
- FAIL_TO_PASS: test_arg
- why: file not named in issue -> search required; tiny fix (+23 lines)

## 6. sympy__sympy-18835 — uniq modifies list argument
- repo: sympy/sympy
- changed: sympy/utilities/iterables.py
- FAIL_TO_PASS: test_uniq
- why: file not named in issue -> search required; tiny fix (+19 lines)

## 7. matplotlib__matplotlib-25332 — [Bug]: Unable to pickle figure with aligned labels
- repo: matplotlib/matplotlib
- changed: lib/matplotlib/cbook.py
- FAIL_TO_PASS: lib/matplotlib/tests/test_pickle.py::test_complete[png]
- why: file not named in issue -> search required; tiny fix (+13 lines)

## 8. matplotlib__matplotlib-23913 — legend draggable as keyword
- repo: matplotlib/matplotlib
- changed: lib/matplotlib/legend.py
- FAIL_TO_PASS: lib/matplotlib/tests/test_legend.py::test_legend_draggable[True], lib/matplotlib/tests/test_legend.py::test_legend_draggable[False]
- why: file not named in issue -> search required; tiny fix (+7 lines)

## 9. pytest-dev__pytest-5103 — Unroll the iterable for all/any calls to get better reports
- repo: pytest-dev/pytest
- changed: src/_pytest/assertion/rewrite.py
- FAIL_TO_PASS: testing/test_assertrewrite.py::TestAssertionRewrite::test_unroll_expression
- why: file not named in issue -> search required; tiny fix (+25 lines)

## 10. sphinx-doc__sphinx-8801 — autodoc: The annotation only member in superclass is treated as "undocumented"
- repo: sphinx-doc/sphinx
- changed: sphinx/ext/autodoc/importer.py
- FAIL_TO_PASS: tests/test_ext_autodoc_autoclass.py::test_uninitialized_attributes
- why: file not named in issue -> search required; tiny fix (+19 lines)

## 11. pallets__flask-4045 — Raise error when blueprint name contains a dot
- repo: pallets/flask
- changed: src/flask/blueprints.py
- FAIL_TO_PASS: tests/test_blueprints.py::test_dotted_name_not_allowed, tests/test_blueprints.py::test_route_decorator_custom_endpoint_with_dots
- why: file not named in issue -> search required; tiny fix (+10 lines)

## 12. pydata__xarray-4493 — DataSet.update causes chunked dask DataArray to evalute its values eagerly 
- repo: pydata/xarray
- changed: xarray/core/variable.py
- FAIL_TO_PASS: xarray/tests/test_variable.py::TestVariable::test_as_variable
- why: file not named in issue -> search required; tiny fix (+10 lines)
