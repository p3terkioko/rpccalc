#include <stdio.h>

int main() {
    int choice;
    double a, b, result;

    while (1) {
        printf("\n===== SIMPLE CALCULATOR =====\n");
        printf("1. Add\n");
        printf("2. Subtract\n");
        printf("3. Multiply\n");
        printf("4. Divide\n");
        printf("5. Exit\n");
        printf("Choose operation: ");
        scanf("%d", &choice);

        if (choice == 5) {
            printf("Exiting calculator. Goodbye!\n");
            break;
        }

        printf("Enter two numbers: ");
        scanf("%lf %lf", &a, &b);

        switch (choice) {
            case 1:
                result = a + b;
                printf("Result: %.2lf\n", result);
                break;
            case 2:
                result = a - b;
                printf("Result: %.2lf\n", result);
                break;
            case 3:
                result = a * b;
                printf("Result: %.2lf\n", result);
                break;
            case 4:
                if (b == 0) {
                    printf("Error: Division by zero!\n");
                } else {
                    result = a / b;
                    printf("Result: %.2lf\n", result);
                }
                break;
            default:
                printf("Invalid choice. Please try again.\n");
        }
    }

    return 0;
}
