#!/usr/bin/env python3
"""
Validate YAML signal mappings against a VSS specification.

For signals that exist in VSS: verifies datatype matches
For custom signals (not in VSS): allows them through with a warning

Usage:
    ./validate_mappings.py --yaml config/model3_mappings_dag.yaml --vss config/vss-5.1-kuksa.json
    ./validate_mappings.py --yaml config/model3_mappings_dag.yaml --vss config/vss-5.1-kuksa.json --strict
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Any

import yaml


# Mapping from YAML datatypes to VSS datatypes
YAML_TO_VSS_TYPES = {
    "boolean": ["boolean"],
    "bool": ["boolean"],
    "float": ["float", "double"],
    "double": ["float", "double"],
    "int": ["int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64"],
    "int8": ["int8"],
    "int16": ["int16"],
    "int32": ["int32"],
    "int64": ["int64"],
    "uint8": ["uint8"],
    "uint16": ["uint16"],
    "uint32": ["uint32"],
    "uint64": ["uint64"],
    "string": ["string"],
    "struct": ["struct"],  # Custom handling needed
}

# VSS types that are compatible with each other
VSS_COMPATIBLE_TYPES = {
    "float": ["float", "double"],
    "double": ["float", "double"],
    "int8": ["int8", "int16", "int32", "int64"],
    "int16": ["int16", "int32", "int64"],
    "int32": ["int32", "int64"],
    "int64": ["int64"],
    "uint8": ["uint8", "uint16", "uint32", "uint64"],
    "uint16": ["uint16", "uint32", "uint64"],
    "uint32": ["uint32", "uint64"],
    "uint64": ["uint64"],
}


class ValidationResult:
    def __init__(self):
        self.errors: list[dict] = []
        self.warnings: list[dict] = []
        self.valid: list[dict] = []
        self.custom: list[dict] = []

    def add_error(self, signal: str, message: str, yaml_type: str = None, vss_type: str = None):
        self.errors.append({
            "signal": signal,
            "message": message,
            "yaml_type": yaml_type,
            "vss_type": vss_type,
        })

    def add_warning(self, signal: str, message: str):
        self.warnings.append({"signal": signal, "message": message})

    def add_valid(self, signal: str, datatype: str):
        self.valid.append({"signal": signal, "datatype": datatype})

    def add_custom(self, signal: str, datatype: str):
        self.custom.append({"signal": signal, "datatype": datatype})

    @property
    def is_valid(self) -> bool:
        return len(self.errors) == 0


def load_yaml_mappings(yaml_path: Path) -> list[dict]:
    """Load signal mappings from YAML file."""
    with open(yaml_path) as f:
        data = yaml.safe_load(f)
    return data.get("mappings", [])


def load_vss_spec(vss_path: Path) -> dict:
    """Load VSS specification from JSON file."""
    with open(vss_path) as f:
        return json.load(f)


def get_vss_signal(vss_spec: dict, signal_path: str) -> dict | None:
    """
    Navigate VSS tree to find a signal by its path.
    Returns the signal definition or None if not found.
    """
    parts = signal_path.split(".")
    current = vss_spec

    for part in parts:
        if "children" in current and part in current["children"]:
            current = current["children"][part]
        elif part in current:
            current = current[part]
        else:
            return None

    return current


def is_type_compatible(yaml_type: str, vss_type: str) -> bool:
    """Check if YAML datatype is compatible with VSS datatype."""
    yaml_type = yaml_type.lower()
    vss_type = vss_type.lower()

    # Direct match
    if yaml_type == vss_type:
        return True

    # Check YAML to VSS mapping
    if yaml_type in YAML_TO_VSS_TYPES:
        if vss_type in YAML_TO_VSS_TYPES[yaml_type]:
            return True

    # Check VSS compatible types (e.g., int8 can fit in int32)
    if yaml_type in VSS_COMPATIBLE_TYPES:
        if vss_type in VSS_COMPATIBLE_TYPES[yaml_type]:
            return True

    return False


def validate_mappings(yaml_path: Path, vss_path: Path, strict: bool = False) -> ValidationResult:
    """
    Validate YAML mappings against VSS specification.

    Args:
        yaml_path: Path to YAML mappings file
        vss_path: Path to VSS JSON specification
        strict: If True, treat custom signals as errors

    Returns:
        ValidationResult with errors, warnings, and valid signals
    """
    result = ValidationResult()

    mappings = load_yaml_mappings(yaml_path)
    vss_spec = load_vss_spec(vss_path)

    for mapping in mappings:
        signal_path = mapping.get("signal")
        yaml_type = mapping.get("datatype")

        if not signal_path:
            result.add_error("(unknown)", "Mapping missing 'signal' field")
            continue

        if not yaml_type:
            result.add_error(signal_path, "Mapping missing 'datatype' field")
            continue

        # Look up signal in VSS
        vss_signal = get_vss_signal(vss_spec, signal_path)

        if vss_signal is None:
            # Signal not in VSS - custom signal
            if strict:
                result.add_error(
                    signal_path,
                    f"Signal not found in VSS specification (strict mode)",
                    yaml_type=yaml_type,
                )
            else:
                result.add_custom(signal_path, yaml_type)
        else:
            # Signal exists in VSS - validate type
            vss_type = vss_signal.get("datatype")

            if vss_type is None:
                # It's a branch, not a signal
                result.add_error(
                    signal_path,
                    f"Path exists in VSS but is a branch, not a signal",
                    yaml_type=yaml_type,
                )
            elif is_type_compatible(yaml_type, vss_type):
                result.add_valid(signal_path, yaml_type)
            else:
                result.add_error(
                    signal_path,
                    f"Type mismatch: YAML has '{yaml_type}', VSS expects '{vss_type}'",
                    yaml_type=yaml_type,
                    vss_type=vss_type,
                )

    return result


def print_results(result: ValidationResult, verbose: bool = False):
    """Print validation results."""
    # Summary
    print("\n" + "=" * 60)
    print("VALIDATION SUMMARY")
    print("=" * 60)
    print(f"  Valid signals:  {len(result.valid)}")
    print(f"  Custom signals: {len(result.custom)}")
    print(f"  Warnings:       {len(result.warnings)}")
    print(f"  Errors:         {len(result.errors)}")
    print()

    # Errors
    if result.errors:
        print("\n" + "-" * 60)
        print("ERRORS (must fix)")
        print("-" * 60)
        for err in result.errors:
            print(f"  X {err['signal']}")
            print(f"     {err['message']}")
            if err.get('yaml_type') and err.get('vss_type'):
                print(f"     YAML: {err['yaml_type']} -> VSS: {err['vss_type']}")
            print()

    # Custom signals (not in VSS)
    if result.custom:
        print("\n" + "-" * 60)
        print("CUSTOM SIGNALS (not in VSS - OK)")
        print("-" * 60)
        for sig in result.custom:
            print(f"  * {sig['signal']} ({sig['datatype']})")
        print()

    # Warnings
    if result.warnings:
        print("\n" + "-" * 60)
        print("WARNINGS")
        print("-" * 60)
        for warn in result.warnings:
            print(f"  ! {warn['signal']}: {warn['message']}")
        print()

    # Valid signals (verbose only)
    if verbose and result.valid:
        print("\n" + "-" * 60)
        print("VALID SIGNALS")
        print("-" * 60)
        for sig in result.valid:
            print(f"  + {sig['signal']} ({sig['datatype']})")
        print()

    # Final status
    print("=" * 60)
    if result.is_valid:
        print("+ VALIDATION PASSED")
    else:
        print("X VALIDATION FAILED")
    print("=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description="Validate YAML signal mappings against VSS specification"
    )
    parser.add_argument(
        "--yaml", "-y",
        required=True,
        help="Path to YAML mappings file"
    )
    parser.add_argument(
        "--vss", "-s",
        required=True,
        help="Path to VSS JSON specification"
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Treat custom signals (not in VSS) as errors"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Show all valid signals"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output results as JSON"
    )

    args = parser.parse_args()

    yaml_path = Path(args.yaml)
    vss_path = Path(args.vss)

    if not yaml_path.exists():
        print(f"Error: YAML file not found: {yaml_path}", file=sys.stderr)
        sys.exit(1)

    if not vss_path.exists():
        print(f"Error: VSS file not found: {vss_path}", file=sys.stderr)
        sys.exit(1)

    result = validate_mappings(yaml_path, vss_path, strict=args.strict)

    if args.json:
        output = {
            "valid": result.is_valid,
            "summary": {
                "valid_count": len(result.valid),
                "custom_count": len(result.custom),
                "warning_count": len(result.warnings),
                "error_count": len(result.errors),
            },
            "errors": result.errors,
            "warnings": result.warnings,
            "custom_signals": result.custom,
            "valid_signals": result.valid,
        }
        print(json.dumps(output, indent=2))
    else:
        print_results(result, verbose=args.verbose)

    sys.exit(0 if result.is_valid else 1)


if __name__ == "__main__":
    main()
