import json
import argparse
from jsonschema import Draft7Validator, FormatChecker

def load_json_file(file_path):
    try:
        with open(file_path, 'r') as f:
            return json.load(f)
    except Exception as e:
        print(f"Failed to load {file_path}: {e}")
        exit(1)

def validate_json(schema_path, data_path):
    schema = load_json_file(schema_path)
    data = load_json_file(data_path)

    validator = Draft7Validator(schema, format_checker=FormatChecker())
    errors = sorted(validator.iter_errors(data), key=lambda e: e.path)

    if not errors:
        print("✅ JSON is valid.")
    else:
        print("❌ JSON validation errors:")
        for error in errors:
            path = ".".join([str(p) for p in error.path])
            print(f"- At '{path}': {error.message}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Validate JSON data against a schema.")
    parser.add_argument("--schema", required=True, help="Path to the JSON schema file.")
    parser.add_argument("--input", required=True, help="Path to the JSON data input file.")

    args = parser.parse_args()
    validate_json(args.schema, args.input)

